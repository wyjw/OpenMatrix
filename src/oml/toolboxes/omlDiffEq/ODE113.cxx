/**
* @file ODE113.cxx
* @date January 2015
* Copyright (C) 2017-2018 Altair Engineering, Inc.  
* This file is part of the OpenMatrix Language ("OpenMatrix") software.
* Open Source License Information:
* OpenMatrix is free software. You can redistribute it and/or modify it under the terms of the GNU Affero General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
* OpenMatrix is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public License for more details.
* You should have received a copy of the GNU Affero General Public License along with this program.  If not, see <http://www.gnu.org/licenses/>.
* 
* Commercial License Information: 
* For a copy of the commercial license terms and conditions, contact the Altair Legal Department at Legal@altair.com and in the subject line, use the following wording: Request for Commercial License Terms for OpenMatrix.
* Altair's dual-license business model allows companies, individuals, and organizations to create proprietary derivative works of OpenMatrix and distribute them - whether embedded or bundled with other software - under a commercial license agreement.
* Use of Altair's trademarks and logos is subject to Altair's trademark licensing policies.  To request a copy, email Legal@altair.com and in the subject line, enter: Request copy of trademark and logo usage policy.
*/

#include "OmlDiffEqTboxFuncs.h"

#include "BuiltInFuncs.h"
#include "BuiltInFuncsUtils.h"
#include "FunctionInfo.h"
#include "OML_Error.h"
#include "StructData.h"

#include "DiffEqFuncs.h"

// file scope variables and functions
static int                 ODE113_sys_size;
static FunctionInfo*       ODE113_sys_func;
static std::string         ODE113_sys_name;
static FUNCPTR             ODE113_sys_pntr;
static EvaluatorInterface* ODE113_eval_ptr;

//------------------------------------------------------------------------------
// Wrapper for the ODE system, used in ODE algorithms called from oml scripts
//------------------------------------------------------------------------------
static int ODE113_file_func(double t, double* y, double* yp, void* userData)
{
    std::vector<Currency> inputs;
    inputs.push_back(t);

    hwMatrix* yMatrix = nullptr;

    if (ODE113_sys_func && ODE113_sys_func->Nargin() > 1)
    {
        yMatrix = new hwMatrix(ODE113_sys_size, 1, hwMatrix::REAL);

        for (int i = 0; i < ODE113_sys_size; ++i)
        {
            (*yMatrix)(i) = y[i];
        }
        inputs.push_back(yMatrix);
    }

    Currency result;

    if (ODE113_sys_func)
    {
        result = ODE113_eval_ptr->CallInternalFunction(ODE113_sys_func, inputs);
    }
    else if (ODE113_sys_pntr)
    {
        result = ODE113_eval_ptr->CallFunction(ODE113_sys_name, inputs);
    }
    else
    {
        return -1;
    }

    if (result.IsMatrix())
    {
        const hwMatrix* ypMatrix = result.Matrix();
        if (!ypMatrix || !ypMatrix->IsReal() || 
            ypMatrix->Size() != ODE113_sys_size)
        {
            return -1;
        }

        for (int i = 0; i < ODE113_sys_size; ++i)
        {
            yp[i] = (*ypMatrix)(i);
        }
    }
    else if (result.IsScalar())
    {
        if (ODE113_sys_size != 1)
        {
            return -1;
        }
        yp[0] = result.Scalar();
    }
    else
    {
        return -1;
    }

    return 0;
}
//------------------------------------------------------------------------------
// Solves a system of non-stiff differential equations - Adams method [ode113]
//------------------------------------------------------------------------------
bool OmlOde113(EvaluatorInterface           eval, 
               const std::vector<Currency>& inputs, 
               std::vector<Currency>&       outputs)
{
    int nargin = eval.GetNarginValue();
    if (nargin < 3)
    {
        throw OML_Error(OML_ERR_NUMARGIN);
    }

    Currency cur1 = inputs[0];
    if (!cur1.IsFunctionHandle())
    {
        throw OML_Error(OML_ERR_HANDLE, 1, OML_VAR_TYPE);
    }    

    FunctionInfo* funcInfo = nullptr;
    FUNCPTR       funcPntr = nullptr;
    std::string   funcName = cur1.FunctionHandle()->FunctionName();
    if (funcName == "anonymous")
    {
        funcInfo = inputs[0].FunctionHandle();
    }
    else if (!eval.FindFunctionByName(funcName, &funcInfo, &funcPntr))
    {
        throw OML_Error(OML_ERR_FUNCNAME, 1);
    }

    if (funcInfo && funcInfo->Parameters().size() != 1 && 
        funcInfo->Parameters().size() != 2)
    {
        throw OML_Error(OML_ERR_NUMARGIN, 1);
    }

    Currency cur2 = inputs[1];
    if (!cur2.IsScalar() && !cur2.IsMatrix())
    {
        throw OML_Error(OML_ERR_SCALARVECTOR, 2);
    }

    Currency cur3 = inputs[2];
    if (!cur3.IsScalar() && !cur3.IsMatrix())
    {
        throw OML_Error(OML_ERR_SCALARVECTOR, 3);
    }

    const hwMatrix* time         = cur2.ConvertToMatrix();
    const hwMatrix* y            = cur3.ConvertToMatrix();
    double          reltol       = 0.001;
    const hwMatrix* abstol       = nullptr;
    double          maxstep      = -999.0;
    bool            deleteAbsTol = false;

    if (nargin > 3)
    {
        if (inputs[3].IsStruct())
        {
            StructData* options = inputs[3].Struct();
            if (options->N() != 1)
            {
                throw OML_Error(OML_ERR_OPTION, 4);
            }
            const Currency& reltol_C  = options->GetValue(0, -1, "RelTol");
            const Currency& abstol_C  = options->GetValue(0, -1, "AbsTol");
            const Currency& maxstep_C = options->GetValue(0, -1, "MaxStep");

            if (!reltol_C.IsEmpty())
            {
                if (reltol_C.IsScalar())
                {
                    reltol = reltol_C.Scalar();
                }
                else
                {
                    throw OML_Error(OML_ERR_SCALARVECTOR, 4, OML_VAR_RELTOL);
                }
            }

            if (abstol_C.IsEmpty())
            {
                abstol = EvaluatorInterface::allocateMatrix(1, 1, 1.0e-6);
                deleteAbsTol = true;
            }
            else if (abstol_C.IsScalar() || abstol_C.IsMatrix())
            {
                abstol = abstol_C.ConvertToMatrix();
            }
            else
            {
                throw OML_Error(OML_ERR_SCALARVECTOR, 4, OML_VAR_ABSTOL);
            }

            if (!maxstep_C.IsEmpty())
            {
                if (maxstep_C.IsScalar())
                {
                    maxstep = maxstep_C.Scalar();
                }
                else
                {
                    throw OML_Error(OML_ERR_SCALAR, 4, OML_VAR_MAXSTEP);
                }
            }

        }
        else if (inputs[3].IsMatrix())
        {
            if (inputs[3].Matrix()->M() != 0 || inputs[3].Matrix()->M() != 0)
            {
                throw OML_Error(OML_ERR_HANDLE_EMPTY, 3);
            }
        }
        else
        {
            throw OML_Error(OML_ERR_HANDLE_EMPTY, 3);
        }
    }

    if (nargin > 4)
    {
        throw OML_Error(OML_ERR_NUMARGIN);
    }
    // set file scope variables
    ODE113_sys_func = funcInfo;
    ODE113_sys_name = funcName;
    ODE113_sys_pntr = funcPntr;
    ODE113_sys_size = y->Size();
    ODE113_eval_ptr = &eval;

    // call algorithm
    hwMatrix*       timeSolution = nullptr;
    hwMatrix*       ySolution    = new hwMatrix;
    hwMatrix*       userData     = nullptr;
    CVRootFn_client rootFunc     = nullptr;
    hwMathStatus    status;

    if (time->Size() == 2)
    {
        // one step mode
        timeSolution = new hwMatrix;
        status = ODE11(ODE113_file_func, rootFunc, *time, *y, timeSolution,
                       *ySolution, reltol, abstol, maxstep, userData);
    }
    else
    {
        // interval mode
        timeSolution = (hwMatrix*) time;
        timeSolution->IncrRefCount();
        status = ODE11(ODE113_file_func, rootFunc, *time, *y, nullptr, *ySolution,
                       reltol, abstol, maxstep, userData);
    }

    if (deleteAbsTol)
    {
        delete abstol;
        abstol = nullptr;
    }

    if (!status.IsOk())
    {
        if (time->Size() == 2)
        {
            delete timeSolution;
            timeSolution = nullptr;
        }
        else
        {
            timeSolution->DecrRefCount();
        }

        if (status.GetArg1() == 111)
        {
            status.SetUserFuncName(funcName);
            status.SetArg1(1);
        }
        else if (status.GetArg1() == 3)
        {
            status.SetArg1(2);
        }
        else if (status.GetArg1() == 4)
        {
            status.SetArg1(3);
        }
        else if (status.GetArg1() > 6 && status.GetArg1() < 9)
        {
            status.SetArg1(4);
        }
        else
        {
            status.ResetArgs();
        }

        if (status.GetArg2() > 6 && status.GetArg2() < 9)
        {
            status.SetArg2(4);
        }
        if (status.IsWarning())
        {
            BuiltInFuncsUtils::SetWarning(eval, status.GetMessageString());
        }
        else
        {
            delete ySolution;
            ySolution = nullptr;
            throw OML_Error(status);
            return false;
        }
    }

    // pack outputs
    outputs.push_back(timeSolution);
    outputs.push_back(ySolution);

    return true;
}
