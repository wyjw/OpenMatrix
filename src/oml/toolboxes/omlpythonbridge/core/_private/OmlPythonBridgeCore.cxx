/**
* @file OmlPythonBridgeCore.cxx
* @date February, 2019
* Copyright (C) 2015-2019 Altair Engineering, Inc.
* This file is part of the OpenMatrix Language (�OpenMatrix�) software.
* Open Source License Information:
* OpenMatrix is free software. You can redistribute it and/or modify it under the terms of the GNU Affero General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
* OpenMatrix is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public License for more details.
* You should have received a copy of the GNU Affero General Public License along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
* Commercial License Information:
* For a copy of the commercial license terms and conditions, contact the Altair Legal Department at Legal@altair.com and in the subject line, use the following wording: Request for Commercial License Terms for OpenMatrix.
* Altair�s dual-license business model allows companies, individuals, and organizations to create proprietary derivative works of OpenMatrix and distribute them - whether embedded or bundled with other software - under a commercial license agreement.
* Use of Altair�s trademarks and logos is subject to Altair's trademark licensing policies.  To request a copy, email Legal@altair.com and in the subject line, enter: Request copy of trademark and logo usage policy.
*/

// Begin defines/includes
/* Remove it if debug version of python is available. */
#if defined(_DEBUG)
#   undef _DEBUG
#   define PYTHON_DEBUG
#endif
/* End change */

#include "Python.h"
#include "arrayobject.h"

/* Remove it if debug version of python is available. */
#if defined(PYTHON_DEBUG)
#   define _DEBUG 1
#endif
/* End change */
#include "hwMatrix.h"
#include "StructData.h"
#include "EvaluatorInt.h"
#include "OmlPythonBridgeCore.h"

// End defines/includes

OmlPythonBridgeCore* OmlPythonBridgeCore::_instance = NULL;

OmlPythonBridgeCore* OmlPythonBridgeCore::GetInstance()
{
    if (!_instance)
    {
        _instance = new OmlPythonBridgeCore();
    }
    return _instance;
}

void OmlPythonBridgeCore::ReleaseInstance()
{
    delete _instance;
    _instance = NULL;
}

//! Constructor
OmlPythonBridgeCore::OmlPythonBridgeCore() : m_errorMessagePython("")
{
}

//! Destructor
OmlPythonBridgeCore::~OmlPythonBridgeCore()
{
}

void OmlPythonBridgeCore::SetErrorMessage(const std::string &error)
{
    m_errorMessagePython = error;
}

std::string OmlPythonBridgeCore::GetErrorMessage()
{
    return m_errorMessagePython;
}

bool OmlPythonBridgeCore::ConvertCurrencyToPyObject(PyObject*& obj, const Currency& var)
{
    
    if (var.IsScalar())
    {
        if (var.IsLogical())
        {
            obj = PyBool_FromLong(var.Scalar());
        }
        //ToDo: uncomment following code once oml interpreter supports true integer
        //else if (var.IsInteger())
        //{
        //	obj = PyInt_FromLong(var.Scalar());
        //}
        else
        {
            obj = PyFloat_FromDouble(var.Scalar());
        }
    }
    else if (var.IsString())
    {
        obj = PyString_FromString((var.StringVal()).c_str());
    }
    else if (var.IsComplex())
    {
        double real = var.Real();
        double imag = var.Imag();
        obj = PyComplex_FromDoubles(real, imag);
    }
    else if (var.IsMatrix())
    {
        const hwMatrix* data = var.Matrix();
        import_array();
        npy_intp dims[2] = { data->M(),data->N() };
        int nd = 2;

        if (data->IsReal())
        {
            obj = PyArray_ZEROS(nd, dims, NPY_DOUBLE, 1);
            double* dptr = (npy_double *)PyArray_DATA(obj);

            for (int i = 0; i < data->Size(); i++)
            {
                dptr[i] = (*data)(i);
            }
        }
        else
        {
            const hwTComplex<double>* cmpxdata = data->GetComplexData();
            obj = PyArray_ZEROS(nd, dims, NPY_COMPLEX128, 1);
            npy_complex128* outdata = (npy_complex128 *)PyArray_DATA(obj);

            for (int i = 0; i < data->Size(); i++)
            {
                outdata[i].real = (cmpxdata[i]).Real();
                outdata[i].imag = (cmpxdata[i]).Imag();
            }
        }
    }
    else if (var.IsFunctionHandle())
    {
        //ToDo:implement when in need
    }
    else if (var.IsStruct())
    {
        const StructData* data = var.Struct();
        if ((1 == data->M()) && (1 == data->N()))
        {
            PyObject* dict = PyDict_New();
            bool status = true;

            if (NULL != dict)
            {
                const std::map<std::string, int>& fieldnames = data->GetFieldNames();

                for (std::map<std::string, int>::const_iterator it = fieldnames.begin(); it != fieldnames.end(); ++it)
                {
                    const Currency& val = data->GetValue(0, 0, it->first);
                    PyObject* value_obj = NULL;
                    status = ConvertCurrencyToPyObject(value_obj, val);

                    if (!status)
                    {
                        Py_XDECREF(dict);
                        break;
                    }

                    if (-1 == PyDict_SetItemString(dict, (it->first).c_str(), value_obj))
                    {
                        Py_XDECREF(dict);
                        status = false;
                        break;
                    }
                }
            }

            if (status)
            {
                obj = dict;
            }
        }
        else
        {
            SetErrorMessage("Multi dimension struct export is not supported.");
        }
        //ToDo:implement struct array exporting when in need
    }
    else if (var.IsCellArray())
    {
        const HML_CELLARRAY* cell = var.CellArray();
        //cell of size 1,n only exported to list
        if (1 == cell->M())
        {
            bool status = true;
            int size = cell->N();
            PyObject* lst = PyList_New(size);

            if (NULL != lst)
            {
                for (int index = 0; index < size; index++)
                {
                    PyObject* valueObj = NULL;
                    const Currency& val = (*cell)(0, index);
                    status = ConvertCurrencyToPyObject(valueObj, val);

                    if (!status)
                    {
                        Py_XDECREF(lst);
                        break;
                    }

                    if (-1 == PyList_SetItem(lst, index, valueObj))
                    {
                        Py_XDECREF(lst);
                        status = false;
                        break;
                    }
                }

                if (status)
                {
                    obj = lst;
                }
            }
        }
        else if (0 == cell->M())
        {
            PyObject* lst = PyList_New(0);

            if (NULL != lst)
            {
                obj = lst;
            }
        }
        else
        {
            SetErrorMessage("Multi dimension cell export is not supported.");
        }

        //ToDo:implement when in need
    }
    else if (var.IsNDMatrix())
    {
        import_array();
        const hwMatrixN* data = var.MatrixN();
        std::vector<int> dimensions = data->Dimensions();
        npy_intp* dims = new npy_intp[dimensions.size()];
        size_t nd = dimensions.size();

        for (int i = 0; i < nd; i++)
        {
            dims[i] = dimensions[i];
        }

        if (data->IsReal())
        {
            obj = PyArray_ZEROS(nd, dims, NPY_DOUBLE, 1);
            double* dptr = (npy_double *)PyArray_DATA(obj);

            for (int i = 0; i < data->Size(); i++)
            {
                dptr[i] = (*data)(i);
            }
        }
        else
        {
            obj = PyArray_ZEROS(nd, dims, NPY_COMPLEX128, 1);
            npy_complex128* outdata = (npy_complex128 *)PyArray_DATA(obj);

            for (int i = 0; i < data->Size(); i++)
            {
                outdata[i].real = (data->z(i)).Real();
                outdata[i].imag = (data->z(i)).Imag();
            }
        }
    }
    else if (var.IsBoundObject())
    {
        //ToDo:implement when in need
    }
    
    return (NULL != obj) ? (true) : (false);
}

bool OmlPythonBridgeCore::ConvertPyObjectToCurrency(std::vector<Currency>& outputs, PyObject* const& obj)
{
    bool success = true;
    
    if (PyDict_Check(obj))
    {
        bool status = true;
        StructData*  dict = EvaluatorInterface::allocateStruct();
        PyObject* keys = PyDict_Keys(obj);
        dict->Dimension(0, 0);

        if (keys)
        {
            Py_ssize_t size = PyList_GET_SIZE(keys);

            for (Py_ssize_t index = 0; index < size; index++)
            {
                PyObject* key = PyList_GetItem(keys, index);
                std::vector<Currency> field;
                std::vector<Currency> value;
                status = ConvertPyObjectToCurrency(field, key);

                if (!status)
                {
                    status = false;
                    break;
                }

                PyObject* value_obj = PyDict_GetItem(obj, key);
                status = ConvertPyObjectToCurrency(value, value_obj);

                if (!status)
                {
                    status = false;
                    break;
                }

                if (field[0].IsCharacter())
                {
                    dict->SetValue(0, 0, field[0].StringVal(), value[0]);
                }
                else if (field[0].IsString())
                {
                    dict->SetValue(0, 0, field[0].StringVal(), value[0]);
                }
                else
                {
                    status = false;
                    break;
                }
            }
        }

        Py_XDECREF(keys);

        if (status)
        {
            outputs.push_back(dict);
        }
        else
        {
            success = false;
            outputs.push_back("");
            delete dict;
        }
    }
    else if (PyList_Check(obj))
    {
        Py_ssize_t size = PyList_GET_SIZE(obj);
        HML_CELLARRAY* list = EvaluatorInterface::allocateCellArray(1, size);
        bool status = true;
        for (Py_ssize_t index = 0; index < size; index++)
        {
            std::vector<Currency> elements;
            PyObject* value = PyList_GetItem(obj, index);
            status = ConvertPyObjectToCurrency(elements, value);

            if (!status)
            {
                break;
            }
            (*list)(0, index) = elements[0];
        }

        if (status)
        {
            outputs.push_back(list);
        }
        else
        {
            success = false;
            outputs.push_back("");
            delete list;
        }
    }
    else if (PyTuple_Check(obj) || PyAnySet_Check(obj))
    {
        //ToDo:Implement once find matching data type available in OML
        success = false;
        outputs.push_back("");
    }
    else if (PyBool_Check(obj))
    {
        outputs.push_back(PyObject_IsTrue(obj) == 1);
    }
    else if (PyUnicode_Check(obj))
    {
        PyObject* bytesObj = PyUnicode_AsASCIIString(obj);

        if (!bytesObj)
        {
            success = false;
            outputs.push_back("");
        }
        else
        {
            outputs.push_back(PyBytes_AsString(bytesObj));
        }

        Py_XDECREF(bytesObj);
    }
    else if (PyFloat_Check(obj))
    {
        outputs.push_back((double)PyFloat_AsDouble(obj));
    }
    else if (PyLong_Check(obj))
    {
        outputs.push_back((double)PyLong_AsLong(obj));
    }
#if PY_MAJOR_VERSION < 3
    else if (PyInt_Check(obj))
    {
        outputs.push_back((int)PyInt_AsLong(obj));
    }
#endif
    else if (PyComplex_Check(obj))
    {
        hwTComplex<double> cplx;
        cplx.Set(PyComplex_RealAsDouble(obj), PyComplex_ImagAsDouble(obj));
        outputs.push_back(cplx);
    }
    else if (PyString_Check(obj))
    {
        outputs.push_back(PyBytes_AsString(obj));
    }
    else
    {
        import_array();
        if (PyArray_Check(obj))
        {
            int nd = PyArray_NDIM((PyArrayObject *)obj);
            npy_intp* dimenstions = PyArray_DIMS((PyArrayObject *)obj);
            int size = PyArray_SIZE(obj);
            std::vector<int> dims;
            bool hasZeroDimSize = false;

            if (nd > 1)
            {
                for (int i = 0; i < nd; i++)
                {
                    dims.push_back(dimenstions[i]);
                    if (0 == dimenstions[i])
                        hasZeroDimSize = true;
                }
            }
            else if (1 == nd)
            {
                dims.push_back(1);
                dims.push_back(dimenstions[0]);

                if (0 == dimenstions[0])
                    hasZeroDimSize = true;
            }

            int type = PyArray_TYPE((PyArrayObject *)obj);
            hwMatrixN* matN = EvaluatorInterface::allocateMatrixN();
            NpyIter* iter = NULL;

            if (!hasZeroDimSize)
                iter = NpyIter_New((PyArrayObject *)obj, NPY_ITER_READONLY | NPY_ITER_ALIGNED, NPY_FORTRANORDER, NPY_NO_CASTING, NULL);

            if (NULL != iter)
            {
                NpyIter_IterNextFunc* iternext = NpyIter_GetIterNext(iter, NULL);

                if (NULL != iternext)
                {
                    int i = 0;
                    char** dataptr = NpyIter_GetDataPtrArray(iter);

                    if (PyArray_ISCOMPLEX((PyArrayObject *)obj))
                    {
                        matN->Dimension(dims, hwMatrixN::COMPLEX);
                        PyObject* obj_itr = PyArray_IterNew(obj);

                        do
                        {
                            char* data = *dataptr;

                            switch (type)
                            {
                            case NPY_CFLOAT:
                            {
                                npy_cfloat* outdata = (npy_cfloat*)data;
                                matN->z(i) = hwTComplex<double>((*outdata).real, (*outdata).imag);
                                break;
                            }

                            case NPY_CDOUBLE:
                            {
                                npy_cdouble* outdata = (npy_cdouble*)data;
                                matN->z(i) = hwTComplex<double>((*outdata).real, (*outdata).imag);
                                break;
                            }

                            case NPY_CLONGDOUBLE:
                            {
                                npy_clongdouble*outdata = (npy_clongdouble*)data;
                                matN->z(i) = hwTComplex<double>((*outdata).real, (*outdata).imag);
                                break;
                            }
                            default:
                            {
                                //ToDo:implement when in need
                            }
                            }

                            ++i;

                        } while (iternext(iter));

                        outputs.push_back(matN);
                    }
                    else if (PyArray_ISNUMBER((PyArrayObject *)obj))
                    {
                        matN->Dimension(dims, hwMatrixN::REAL);

                        do
                        {
                            char* data = *dataptr;

                            switch (type)
                            {
                            case NPY_BOOL:
                            {
                                npy_bool* outdata = (npy_bool*)data;
                                (*matN)(i) = (*outdata);
                                break;
                            }
                            case NPY_BYTE:
                            {
                                npy_byte* outdata = (npy_byte*)data;
                                (*matN)(i) = (*outdata);
                                break;
                            }
                            case NPY_UBYTE:
                            {
                                npy_ubyte* outdata = (npy_ubyte*)data;
                                (*matN)(i) = (*outdata);
                                break;
                            }
                            case NPY_SHORT:
                            {
                                npy_short* outdata = (npy_short*)data;
                                (*matN)(i) = (*outdata);
                                break;
                            }
                            case NPY_USHORT:
                            {
                                npy_ushort* outdata = (npy_ushort*)data;
                                (*matN)(i) = (*outdata);
                                break;
                            }
                            case NPY_INT:
                            {
                                npy_int* outdata = (npy_int*)data;
                                (*matN)(i) = (*outdata);
                                break;
                            }
                            case NPY_UINT:
                            {
                                npy_uint* outdata = (npy_uint*)data;
                                (*matN)(i) = (*outdata);
                                break;
                            }
                            case NPY_LONG:
                            {
                                npy_long* outdata = (npy_long*)data;
                                (*matN)(i) = (*outdata);
                                break;
                            }
                            case NPY_ULONG:
                            {
                                npy_ulong* outdata = (npy_ulong*)data;
                                (*matN)(i) = (*outdata);
                                break;
                            }
                            case NPY_LONGLONG:
                            {
                                npy_longlong* outdata = (npy_longlong*)data;
                                (*matN)(i) = (*outdata);
                                break;
                            }
                            case NPY_ULONGLONG:
                            {
                                npy_ulonglong* outdata = (npy_ulonglong*)data;
                                (*matN)(i) = (*outdata);
                                break;
                            }
                            case NPY_FLOAT:
                            {
                                npy_float* outdata = (npy_float*)data;
                                (*matN)(i) = (*outdata);
                                break;
                            }
                            case NPY_DOUBLE:
                            {
                                npy_double* outdata = (npy_double*)data;
                                (*matN)(i) = (*outdata);
                                break;
                            }
                            case NPY_LONGDOUBLE:
                            {
                                npy_longdouble* outdata = (npy_longdouble*)data;
                                (*matN)(i) = (*outdata);
                                break;
                            }
                            default:
                            {
                                //ToDo:implement when in need
                            }
                            }

                            ++i;

                        } while (iternext(iter));

                        outputs.push_back(matN);
                    }
                    else
                    {
                        success = false;
                        outputs.push_back("");
                    }
                }
                else
                {
                    success = false;
                    outputs.push_back("");
                }

                NpyIter_Deallocate(iter);
            }
            else
            {
                success = true;
                matN->Dimension(dims, hwMatrixN::REAL);
                outputs.push_back(matN);
            }
        }
        else
        {
            success = false;
            outputs.push_back("");
        }
    }
    
    return success;
}

std::string OmlPythonBridgeCore::GetPyObjectAsString(PyObject* const& obj)
{
    std::string value = "";
    
    PyObject* strobj = PyObject_Str(obj);

    if (NULL != strobj)
    {
#ifdef IS_PY3
        PyObject* bytesObj = PyUnicode_AsUTF8String(strobj);
        if (bytesObj)
        {
            value = PyBytes_AS_STRING(bytesObj);
        }
        Py_XDECREF(bytesObj);
#else
        value = PyString_AsString(strobj);
#endif
    }

    Py_XDECREF(strobj);
    
    return value;
}

void OmlPythonBridgeCore::HandleException(void)
{
    
    PyObject * expn = NULL, *xval = NULL, *xtrc = NULL;
    PyErr_Fetch(&expn, &xval, &xtrc);

    if (NULL != expn)
    {
        std::string errorMessage = "Unknown Exception";

        if (NULL != xval)
        {
            errorMessage = GetPyObjectAsString(xval);
        }
        SetErrorMessage(errorMessage);
        PyErr_Restore(expn, xval, xtrc);

        if (PyErr_Occurred())
        {
            PyErr_Clear();
        }
    }
    
}

bool OmlPythonBridgeCore::IsTypeSupported(PyObject* const& obj)
{
    bool is_supported = false;
    
    if (PyString_Check(obj) || PyFloat_Check(obj) || PyLong_Check(obj) || PyBool_Check(obj) || PyComplex_Check(obj))
    {
        is_supported = true;
    }
    else if (PyDict_Check(obj))
    {
        PyObject* keys = PyDict_Keys(obj);
        bool areKeysValid = true;

        if (keys)
        {
            Py_ssize_t size = PyList_GET_SIZE(keys);

            for (Py_ssize_t index = 0; index < size; index++)
            {
                PyObject* key = PyList_GetItem(keys, index);

                if (!(PyUnicode_Check(key) || PyString_Check(key)))
                {
                    areKeysValid = false;
                    break;
                }
            }
        }

        Py_XDECREF(keys);

        if (areKeysValid)
        {
            PyObject* values = PyDict_Values(obj);
            is_supported = true;

            if (values)
            {
                Py_ssize_t size = PyList_GET_SIZE(values);

                for (Py_ssize_t index = 0; index < size; index++)
                {
                    PyObject* value = PyList_GetItem(values, index);

                    if (!IsTypeSupported(value))
                    {
                        is_supported = false;
                        break;
                    }
                }
            }
            Py_XDECREF(values);
        }
    }
    else if (PyList_Check(obj))
    {
        Py_ssize_t size = PyList_GET_SIZE(obj);
        is_supported = true;

        for (Py_ssize_t index = 0; index < size; index++)
        {
            PyObject* value = PyList_GetItem(obj, index);

            if (!IsTypeSupported(value))
            {
                is_supported = false;
                break;
            }
        }
    }
    else if (PyUnicode_Check(obj))
    {
        PyObject *bytesObj = PyUnicode_AsASCIIString(obj);

        if (bytesObj)
        {
            is_supported = true;
        }
        Py_XDECREF(bytesObj);
    }
#if PY_MAJOR_VERSION < 3
    else if (PyInt_Check(obj))
    {
        is_supported = true;
    }
#endif
    else {
        import_array();
        if (PyArray_Check(obj))
        {
            is_supported = true;
        }
    }
    
    return is_supported;
}
