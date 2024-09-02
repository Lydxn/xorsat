#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stddef.h>           /* offsetof() */
#include <m4ri/m4ri.h>
#include "structmember.h"     /* PyMemberDef */
#include "xorsatmodule.h"


static PyTypeObject BitExpr_Type, BitSet_Type, BitVec_Type, BitVecConstraint_Type,
                    Constraint_Type, LinearSystem_Type, VarInfo_Type;

/* ================================ VarInfo ================================= */

PyObject *
varinfo_create(PyTypeObject *type, PyObject *name, Py_ssize_t bits, Py_ssize_t offset)
{
    VarInfoObject *obj;

    obj = (VarInfoObject *)type->tp_alloc(type, 0);
    if (obj == NULL)
        return NULL;

    obj->name = Py_NewRef(name);
    obj->bits = bits;
    obj->offset = offset;
    return (PyObject *)obj;
}

static PyObject *
varinfo_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *name;
    Py_ssize_t bits, offset;

    if (!PyArg_ParseTuple(args, "Unn", &name, &bits, &offset))
        return NULL;
    return varinfo_create(type, name, bits, offset);
}

static PyObject *
varinfo_repr(VarInfoObject *self)
{
    return PyUnicode_FromFormat("<VarInfo '%U'>", self->name);
}

static void
varinfo_dealloc(VarInfoObject *self)
{
    Py_XDECREF(self->name);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyMemberDef varinfo_members[] = {
    { "name", T_OBJECT_EX, offsetof(VarInfoObject, name), READONLY, NULL },
    { "bits", T_PYSSIZET, offsetof(VarInfoObject, bits), READONLY, NULL },
    { "offset", T_PYSSIZET, offsetof(VarInfoObject, offset), READONLY, NULL },
    { NULL },
};

static PyTypeObject VarInfo_Type = {
    .ob_base = PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "xorsat.VarInfo",
    .tp_basicsize = sizeof(VarInfoObject),
    .tp_itemsize = 0,
    .tp_dealloc = (destructor)varinfo_dealloc,
    .tp_repr = (reprfunc)varinfo_repr,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_doc = NULL,
    .tp_members = varinfo_members,
    .tp_new = varinfo_new,
};

/* ================================= BitRef ================================= */

PyObject *
bitref_create(PyTypeObject *type, VarInfoObject *var, Py_ssize_t index)
{
    BitRefObject *obj;

    obj = (BitRefObject *)type->tp_alloc(type, 0);
    if (obj == NULL)
        return NULL;

    obj->var = Py_NewRef(var);
    obj->index = index;
    return (PyObject *)obj;
}

static PyObject *
bitref_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    VarInfoObject *var;
    Py_ssize_t index;

    if (!PyArg_ParseTuple(args, "O!n", &VarInfo_Type, &var, &index))
        return NULL;
    if (index < 0 || index >= var->bits) {
        PyErr_SetString(PyExc_IndexError, "index out of range");
        return NULL;
    }
    return bitref_create(type, var, index);
}

static PyObject *
bitref_repr(BitRefObject *self)
{
    VarInfoObject *var = (VarInfoObject *)self->var;
    return PyUnicode_FromFormat("Bit %zd of %S", self->index, var->name);
}

static void
bitref_dealloc(BitRefObject *self)
{
    Py_XDECREF(self->var);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyMemberDef bitref_members[] = {
    { "var", T_OBJECT_EX, offsetof(BitRefObject, var), READONLY, NULL },
    { "index", T_PYSSIZET, offsetof(BitRefObject, index), READONLY, NULL },
    { NULL },
};

static PyTypeObject BitRef_Type = {
    .ob_base = PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "xorsat.BitRef",
    .tp_basicsize = sizeof(BitRefObject),
    .tp_itemsize = 0,
    .tp_dealloc = (destructor)bitref_dealloc,
    .tp_repr = (reprfunc)bitref_repr,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_doc = NULL,
    .tp_members = bitref_members,
    .tp_new = bitref_new,
};

/* ================================= BitSet ================================= */

uint8_t
bitset_test(BitSetObject *bs, Py_ssize_t i)
{
    return (bs->buf[i / WORD_SIZE] >> (i % WORD_SIZE)) & 1;
}

void
bitset_set(BitSetObject *bs, Py_ssize_t i, uint8_t val)
{
    bs->buf[i / WORD_SIZE] |= (bitset_t)val << (i % WORD_SIZE);
}

PyObject *
bitset_from_size(PyTypeObject *type, Py_ssize_t bits, int clear)
{
    BitSetObject *obj;
    Py_ssize_t size;

    size = BS_SIZE(bits);
    obj = PyObject_NewVar(BitSetObject, type, size);
    if (obj == NULL)
        return NULL;
    obj->bits = bits;

    if (clear)
        memset(obj->buf, 0, size * sizeof(bitset_t));
    return (PyObject *)obj;
}

void
bitset_inplace_xor(BitSetObject *a, BitSetObject *b)
{
    Py_ssize_t i;

    assert(a->bits == b->bits);
    for (i = 0; i < Py_SIZE(a); i++)
        a->buf[i] ^= b->buf[i];
}

// Assumes that a and b are the same size
PyObject *
bitset_xor_impl(BitSetObject *a, BitSetObject *b)
{
    BitSetObject *result;
    Py_ssize_t i;

    assert(a->bits == b->bits);
    result = (BitSetObject *)bitset_from_size(&BitSet_Type, a->bits, 0);
    if (result == NULL)
        return NULL;

    for (i = 0; i < Py_SIZE(result); i++)
        result->buf[i] = a->buf[i] ^ b->buf[i];
    return (PyObject *)result;
}

Py_ssize_t
bitset_count_impl(BitSetObject *bs)
{
    Py_ssize_t result, i;

    result = 0;
    for (i = 0; i < Py_SIZE(bs); i++)
        result += __builtin_popcountll(bs->buf[i]);
    return result;
}

static PyObject *
bitset_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *arg;
    Py_ssize_t bits;

    if (!_PyArg_NoKeywords(type->tp_name, kwds))
        return NULL;
    if (!PyArg_UnpackTuple(args, type->tp_name, 1, 1, &arg))
        return NULL;

    bits = PyNumber_AsSsize_t(arg, PyExc_OverflowError);
    if (bits == -1 && PyErr_Occurred())
        return NULL;
    if (bits < 0) {
        PyErr_SetString(PyExc_ValueError, "number of bits cannot be negative");
        return NULL;
    }
    return bitset_from_size(type, bits, 1);
}

static Py_ssize_t
bitset_length(BitSetObject *self)
{
    return self->bits;
}

static PyObject *
bitset_item(BitSetObject *self, Py_ssize_t i)
{
    if (i < 0 || i >= self->bits) {
        PyErr_SetString(PyExc_IndexError, "BitSet index out of range");
        return NULL;
    }
    return PyLong_FromLong(bitset_test(self, i));
}

static PyObject *
bitset_repr(BitSetObject *self)
{
    char *str;
    Py_ssize_t i;
    PyObject *result;

    str = (char *)PyMem_Malloc(self->bits);
    if (str == NULL)
        return PyErr_NoMemory();

    for (i = 0; i < self->bits; i++)
        str[i] = '0' + bitset_test(self, i);

    result = PyUnicode_FromStringAndSize(str, self->bits);
    PyMem_Free(str);
    return result;
}

static PySequenceMethods var_as_sequence = {
    .sq_length = (lenfunc)bitset_length,
    .sq_item = (ssizeargfunc)bitset_item,
};

static PyTypeObject BitSet_Type = {
    .ob_base = PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "xorsat.BitSet",
    .tp_basicsize = sizeof(BitSetObject),
    .tp_itemsize = sizeof(bitset_t),
    .tp_dealloc = (destructor)PyObject_Free,
    .tp_repr = (reprfunc)bitset_repr,
    .tp_as_sequence = &var_as_sequence,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_doc = NULL,
    .tp_new = bitset_new,
};

/* ================================ BitExpr ================================= */

PyObject *
bitexpr_copy(BitExprObject *expr)
{
    BitExprObject *result;

    result = PyObject_GC_New(BitExprObject, &BitExpr_Type);
    if (result == NULL)
        return NULL;

    result->system = Py_NewRef(expr->system);
    result->mask = Py_NewRef(expr->mask);
    result->compl = expr->compl;

    PyObject_GC_Track(result);
    return (PyObject *)result;
}

PyObject *
bitexpr_from_bit(PyTypeObject *type, uint8_t bit, LinearSystemObject *system)
{
    BitExprObject *result;

    result = PyObject_GC_New(BitExprObject, type);
    if (result == NULL)
        return NULL;

    result->mask = bitset_from_size(&BitSet_Type, system->bits, 1);
    if (result->mask == NULL) {
        Py_DECREF(result);
        return NULL;
    }
    result->system = Py_NewRef(system);
    result->compl = bit;

    PyObject_GC_Track(result);
    return (PyObject *)result;
}

int
getbit(PyObject *num, const char *message)
{
    Py_ssize_t value;

    value = PyNumber_AsSsize_t(num, PyExc_ValueError);
    if (value == -1 && PyErr_Occurred())
        return -1;
    if (value != 0 && value != 1) {
        PyErr_SetString(PyExc_ValueError, message);
        return -1;
    }
    return value;
}

static PyObject *
bitexpr_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = { "", "system", NULL };
    LinearSystemObject *system;
    Py_ssize_t compl;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "nO!", kwlist,
                                     &compl, &LinearSystem_Type, &system))
        return NULL;

    if (compl != 0 && compl != 1) {
        PyErr_SetString(PyExc_ValueError, "first argument must be either 0 or 1");
        return NULL;
    }
    return bitexpr_from_bit(type, compl, system);
}

static PyObject *
bitexpr_and(PyObject *a, PyObject *b)
{
    BitExprObject *expr;
    LinearSystemObject *system;
    PyObject *num;
    Py_ssize_t value;

    if (BitExpr_Check(a)) {
        expr = (BitExprObject *)a;
        num = b;
    } else {
        expr = (BitExprObject *)b;
        num = a;
    }

    value = getbit(num, "and operand must be 0, 1, or BitExpr");
    if (value == -1)
        return NULL;
    if (value)
        return bitexpr_copy(expr);
    system = (LinearSystemObject *)expr->system;
    return Py_NewRef(system->_expr_const[0]);
}

static PyObject *
bitexpr_or(PyObject *a, PyObject *b)
{
    BitExprObject *expr;
    LinearSystemObject *system;
    PyObject *num;
    Py_ssize_t value;

    if (BitExpr_Check(a)) {
        expr = (BitExprObject *)a;
        num = b;
    } else {
        expr = (BitExprObject *)b;
        num = a;
    }

    value = getbit(num, "or operand must be 0, 1, or BitExpr");
    if (value == -1)
        return NULL;
    if (value) {
        system = (LinearSystemObject *)expr->system;
        return Py_NewRef(system->_expr_const[1]);
    }
    return bitexpr_copy(expr);
}

PyObject *
bitexpr_xor_bit(BitExprObject *expr, uint8_t bit)
{
    BitExprObject *result;

    result = (BitExprObject *)bitexpr_copy(expr);
    result->compl ^= bit;
    return (PyObject *)result;
}

PyObject *
bitexpr_xor_number(BitExprObject *expr, PyObject *num)
{
    Py_ssize_t value;

    value = getbit(num, "xor operand must be 0, 1, or BitExpr");
    if (value == -1)
        return NULL;
    return bitexpr_xor_bit(expr, value);
}

PyObject *
bitexpr_xor_bitexpr(BitExprObject *a, BitExprObject *b)
{
    BitExprObject *result;

    if (!Py_Is(a->system, b->system)) {
        PyErr_SetString(PyExc_TypeError,
            "cannot xor BitVecs in different linear systems");
        return NULL;
    }

    result = PyObject_GC_New(BitExprObject, &BitExpr_Type);
    if (result == NULL)
        return NULL;

    result->mask = bitset_xor_impl((BitSetObject *)a->mask, (BitSetObject *)b->mask);
    if (result->mask == NULL) {
        Py_DECREF(result);
        return NULL;
    }
    result->system = Py_NewRef(a->system);
    result->compl = a->compl ^ b->compl;

    PyObject_GC_Track(result);
    return (PyObject *)result;
}

static PyObject *
bitexpr_xor(PyObject *a, PyObject *b)
{
    if (!BitExpr_Check(b))
        return bitexpr_xor_number((BitExprObject *)a, b);
    else if (!BitExpr_Check(a))
        return bitexpr_xor_number((BitExprObject *)b, a);
    return bitexpr_xor_bitexpr((BitExprObject *)a, (BitExprObject *)b);
}

static PyObject *
bitexpr_invert(BitExprObject *self)
{
    return bitexpr_xor_bit(self, 1);
}

static PyObject *
bitexpr_terms(BitExprObject *self)
{
    LinearSystemObject *system;
    VarInfoObject *var;
    PyObject *result, *term;
    Py_ssize_t num_terms, i, b, resi;

    num_terms = bitset_count_impl((BitSetObject *)self->mask);
    result = PyTuple_New(num_terms);
    if (result == NULL)
        return NULL;
    resi = 0;

    system = (LinearSystemObject *)self->system;
    for (i = 0; i < system->vi_size; i++) {
        var = (VarInfoObject *)system->vi_table[i];
        for (b = 0; b < var->bits; b++) {
            if (!bitset_test((BitSetObject *)self->mask, var->offset + b))
                continue;

            term = bitref_create(&BitRef_Type, var, b);
            if (term == NULL) {
                Py_DECREF(result);
                return NULL;
            }
            PyTuple_SET_ITEM(result, resi++, term);
        }
    }
    return (PyObject *)result;
}

static PyObject *
bitexpr_is_constant(BitExprObject *self)
{
    if (bitset_count_impl((BitExprObject *)self->mask) == 0)
        Py_RETURN_TRUE;
    Py_RETURN_FALSE;
}

static PyObject *
bitexpr_richcompare(PyObject *self, PyObject *other, int op)
{
    if (op != Py_EQ)
        Py_RETURN_NOTIMPLEMENTED;
    return constraint_create(&Constraint_Type, self, other);
}

static PyObject *
bitexpr_repr(BitExprObject *self)
{
    LinearSystemObject *system;
    VarInfoObject *var;
    _PyUnicodeWriter writer;
    char buf[24];
    Py_ssize_t i, b;
    int first = 1;

    _PyUnicodeWriter_Init(&writer);
    writer.overallocate = 1;

    system = (LinearSystemObject *)self->system;
    for (i = 0; i < system->vi_size; i++) {
        var = (VarInfoObject *)system->vi_table[i];
        for (b = 0; b < var->bits; b++) {
            if (!bitset_test((BitSetObject *)self->mask, var->offset + b))
                continue;

            if (!first) {
                if (_PyUnicodeWriter_WriteASCIIString(&writer, " ^ ", 3) < 0)
                    goto error;
            }
            first = 0;

            if (_PyUnicodeWriter_WriteStr(&writer, var->name) < 0)
                goto error;

            // If the variable is a single bit, do not append a suffix since that would
            // be redundant.
            if (var->bits != 1) {
                int n = snprintf(buf, sizeof(buf), "_%zd", b);
                if (_PyUnicodeWriter_WriteASCIIString(&writer, buf, n) < 0)
                    goto error;
            }
        }
    }

    if (first) {
        if (_PyUnicodeWriter_WriteChar(&writer, '0' + self->compl) < 0)
            goto error;
    } else if (self->compl) {
        if (_PyUnicodeWriter_WriteASCIIString(&writer, " ^ 1", 4) < 0)
            goto error;
    }

    writer.overallocate = 0;
    return _PyUnicodeWriter_Finish(&writer);

error:
    _PyUnicodeWriter_Dealloc(&writer);
    return NULL;
}

static int
bitexpr_traverse(BitExprObject *self, visitproc visit, void *arg)
{
    Py_VISIT(self->system);
    return 0;
}

static int
bitexpr_clear(BitExprObject *self)
{
    Py_CLEAR(self->system);
    return 0;
}

static void
bitexpr_dealloc(BitExprObject *self)
{
    PyObject_GC_UnTrack(self);
    bitexpr_clear(self);
    Py_XDECREF(self->mask);
    PyObject_GC_Del(self);
}

static PyNumberMethods bitexpr_as_number = {
    .nb_invert = (unaryfunc)bitexpr_invert,
    .nb_and = (binaryfunc)bitexpr_and,
    .nb_xor = (binaryfunc)bitexpr_xor,
    .nb_or = (binaryfunc)bitexpr_or,
};

static PyMemberDef bitexpr_members[] = {
    { "system", T_OBJECT_EX, offsetof(BitExprObject, system), READONLY, NULL },
    { "mask", T_OBJECT_EX, offsetof(BitExprObject, mask), READONLY, NULL },
    { "compl", T_BOOL, offsetof(BitExprObject, compl), READONLY, NULL },
    { NULL },
};

static PyMethodDef bitexpr_methods[] = {
    { "terms", (PyCFunction)bitexpr_terms, METH_NOARGS, NULL },
    { "is_constant", (PyCFunction)bitexpr_is_constant, METH_NOARGS, NULL },
    { NULL },
};

static PyTypeObject BitExpr_Type = {
    .ob_base = PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "xorsat.BitExpr",
    .tp_basicsize = sizeof(BitExprObject),
    .tp_itemsize = 0,
    .tp_dealloc = (destructor)bitexpr_dealloc,
    .tp_repr = (reprfunc)bitexpr_repr,
    .tp_as_number = &bitexpr_as_number,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC,
    .tp_doc = NULL,
    .tp_traverse = (traverseproc)bitexpr_traverse,
    .tp_clear = (inquiry)bitexpr_clear,
    .tp_richcompare = (richcmpfunc)bitexpr_richcompare,
    .tp_methods = bitexpr_methods,
    .tp_members = bitexpr_members,
    .tp_new = bitexpr_new,
};

/* ================================= BitVec ================================= */

PyObject *
bitvec_from_size(PyTypeObject *type, Py_ssize_t size, PyObject *system,
                 int zero)
{
    LinearSystemObject *s;
    BitVecObject *obj;
    Py_ssize_t i;

    obj = PyObject_NewVar(BitVecObject, type, size);
    if (obj == NULL)
        return NULL;

    for (i = 0; i < size; i++)
        obj->exprs[i] = NULL;

    if (zero) {
        s = (LinearSystemObject *)system;
        for (i = 0; i < size; i++) {
            obj->exprs[i] = Py_NewRef(s->_expr_const[0]);
            if (obj->exprs[i] == NULL) {
                Py_DECREF(obj);
                return NULL;
            }
        }
    }
    obj->system = Py_NewRef(system);

    return (PyObject *)obj;
}

static PyObject *
bitvec_from_sequence(PyTypeObject *type, PyObject *object)
{
    BitVecObject *result;
    BitExprObject *expr;
    PyObject *seq, **items;
    Py_ssize_t size, i;

    seq = PySequence_Fast(object, "argument is not iterable");
    if (seq == NULL)
        return NULL;

    size = PySequence_Fast_GET_SIZE(seq);
    if (size == 0) {
        PyErr_SetString(PyExc_ValueError, "iterable cannot be empty");
        goto error;
    }

    items = PySequence_Fast_ITEMS(seq);
    for (i = 0; i < size; i++) {
        if (!BitExpr_Check(items[i])) {
            PyErr_Format(PyExc_TypeError,
                "expected iterable of BitVecs, got: '%.200s'",
                Py_TYPE(items[i])->tp_name);
            goto error;
        }
    }

    expr = (BitExprObject *)items[0];
    result = (BitVecObject *)bitvec_from_size(type, size, expr->system, 0);
    if (result == NULL)
        goto error;

    for (i = 0; i < size; i++) {
        expr = (BitExprObject *)items[i];
        if (!Py_Is(expr->system, result->system)) {
            PyErr_SetString(PyExc_TypeError,
                "iterable cannot contain differing linear systems");
            Py_DECREF(result);
            goto error;
        }
        result->exprs[i] = Py_NewRef(expr);
    }

    Py_DECREF(seq);
    return (PyObject *)result;

error:
    Py_DECREF(seq);
    return NULL;
}

static PyObject *
bitvec_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = { "", "system", NULL };
    PyObject *object;
    PyObject *system = NULL;
    Py_ssize_t size;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|O!", kwlist,
                                     &object, &LinearSystem_Type, &system))
        return NULL;

    if (PyIndex_Check(object)) {
        if (system == NULL) {
            PyErr_SetString(PyExc_TypeError,
                "must specify a system argument when initializing an empty BitVec");
            return NULL;
        }
        size = PyNumber_AsSsize_t(object, PyExc_OverflowError);
        if (size == -1 && PyErr_Occurred())
            return NULL;
        if (size <= 0) {
            PyErr_SetString(PyExc_ValueError, "size must be positive");
            return NULL;
        }
        return bitvec_from_size(type, size, system, 1);
    }
    if (system != NULL) {
        PyErr_SetString(PyExc_TypeError,
            "system argument should not be specified when passing an iterable");
        return NULL;
    }
    return bitvec_from_sequence(type, object);
}

PyObject *
bitvec_bitwise_number(BitVecObject *vec, const char op, PyObject *num)
{
    BitVecObject *result = NULL;
    LinearSystemObject *system;
    PyObject *tmp;
    uint8_t *buf, *ptr, byte, b;
    Py_ssize_t size, bytes, i, veci;

    if (!PyLong_Check(num))
        Py_RETURN_NOTIMPLEMENTED;

    size = Py_SIZE(vec);
    bytes = (size + 7) / 8;
    buf = (uint8_t *)PyMem_Malloc(bytes);
    if (_PyLong_AsByteArray((PyLongObject *)num, buf, bytes,
                            PY_LITTLE_ENDIAN, Py_SIZE(num) < 0) < 0)
        goto error;

    result = (BitVecObject *)bitvec_from_size(&BitVec_Type, size, vec->system, 0);
    if (result == NULL)
        goto error;

    system = (LinearSystemObject *)vec->system;
    veci = 0;
    ptr = buf;
    while (1) {
        byte = *ptr++;
        for (i = 8; i; i--) {
            b = byte & 1;
            byte >>= 1;

            switch (op) {
            case '^':
                tmp = bitexpr_xor_bit((BitExprObject *)vec->exprs[veci], b);
                break;
            case '&':
                tmp = Py_NewRef(b ? vec->exprs[veci] : system->_expr_const[0]);
                break;
            case '|':
                tmp = Py_NewRef(b ? system->_expr_const[1] : vec->exprs[veci]);
                break;
            default:
                Py_UNREACHABLE();
            }

            result->exprs[veci++] = tmp;
            if (veci == size)
                goto done;
        }
    }

done:
    PyMem_Free(buf);
    return (PyObject *)result;

error:
    PyMem_Free(buf);
    Py_XDECREF(result);
    return NULL;
}

static PyObject *
bitvec_xor(PyObject *a, PyObject *b)
{
    BitVecObject *result, *x, *y, *tmp;
    Py_ssize_t xsize, ysize, i;

    if (!BitVec_Check(b))
        return bitvec_bitwise_number((BitVecObject *)a, '^', b);
    else if (!BitVec_Check(a))
        return bitvec_bitwise_number((BitVecObject *)b, '^', a);

    x = (BitVecObject *)a;
    y = (BitVecObject *)b;

    // Maintain len(x) >= len(y)
    if (Py_SIZE(x) < Py_SIZE(y)) {
        tmp = x;
        x = y;
        y = tmp;
    }

    xsize = Py_SIZE(x);
    ysize = Py_SIZE(y);

    if (x->system != y->system) {
        PyErr_SetString(PyExc_TypeError,
            "cannot xor BitVecs in different linear systems");
        return NULL;
    }

    result = (BitVecObject *)bitvec_from_size(&BitVec_Type, xsize, x->system, 0);
    if (result == NULL)
        return NULL;

    for (i = 0; i < ysize; i++) {
        result->exprs[i] = bitexpr_xor_bitexpr(
            (BitExprObject *)x->exprs[i],
            (BitExprObject *)y->exprs[i]);
        if (result->exprs[i] == NULL) {
            Py_DECREF(result);
            return NULL;
        }
    }
    for (; i < xsize; i++)
        result->exprs[i] = Py_NewRef(x->exprs[i]);

    return (PyObject *)result;
}

static PyObject *
bitvec_and(PyObject *a, PyObject *b)
{
    if (!BitVec_Check(b))
        return bitvec_bitwise_number((BitVecObject *)a, '&', b);
    else if (!BitVec_Check(a))
        return bitvec_bitwise_number((BitVecObject *)b, '&', a);
    Py_RETURN_NOTIMPLEMENTED;
}

static PyObject *
bitvec_or(PyObject *a, PyObject *b)
{
    if (!BitVec_Check(b))
        return bitvec_bitwise_number((BitVecObject *)a, '|', b);
    else if (!BitVec_Check(a))
        return bitvec_bitwise_number((BitVecObject *)b, '|', a);
    Py_RETURN_NOTIMPLEMENTED;
}

static PyObject *
bitvec_invert(BitVecObject *self)
{
    BitVecObject *result;
    Py_ssize_t size, i;

    size = Py_SIZE(self);
    result = (BitVecObject *)bitvec_from_size(&BitVec_Type, size, self->system, 0);
    if (result == NULL)
        return NULL;
    for (i = 0; i < size; i++) {
        result->exprs[i] = bitexpr_invert((BitExprObject *)self->exprs[i]);
        if (result->exprs[i] == NULL) {
            Py_DECREF(result);
            return NULL;
        }
    }
    return (PyObject *)result;
}

static PyObject *
bitvec_shift(PyObject *a, PyObject *b, shift_t shtype)
{
    BitVecObject *result, *x;
    LinearSystemObject *system;
    PyObject *expr;
    Py_ssize_t shift, size, i, j;

    if (!BitVec_Check(a) || !PyLong_Check(b))
        Py_RETURN_NOTIMPLEMENTED;

    x = (BitVecObject *)a;
    shift = PyLong_AsSsize_t(b);
    if (shift == -1 && PyErr_Occurred())
        return NULL;
    if (shift < 0) {
        PyErr_SetString(PyExc_ValueError, "negative shift count");
        return NULL;
    }

    size = Py_SIZE(x);
    result = (BitVecObject *)bitvec_from_size(&BitVec_Type, size, x->system, 0);
    if (result == NULL)
        return NULL;

    shift %= size;
    system = (LinearSystemObject *)x->system;

    switch (shtype) {
    case SHIFT_SHL:
        shift %= size;
        for (i = 0; i < shift; i++)
            result->exprs[i] = Py_NewRef(system->_expr_const[0]);
        for (j = 0; i < size; i++, j++)
            result->exprs[i] = Py_NewRef(x->exprs[j]);
        break;
    case SHIFT_SAR:
    case SHIFT_SHR:
        for (i = 0, j = shift; j < size; i++, j++)
            result->exprs[i] = Py_NewRef(x->exprs[j]);
        expr = shtype == SHIFT_SAR ? x->exprs[size - 1] : system->_expr_const[0];
        for (; i < size; i++)
            result->exprs[i] = Py_NewRef(expr);
        break;
    case SHIFT_ROL:
    case SHIFT_ROR:
        if (shtype == SHIFT_ROR)
            shift = size - shift;
        for (i = 0, j = size - shift; i < shift; i++, j++)
            result->exprs[i] = Py_NewRef(x->exprs[j]);
        for (j = 0; i < size; i++, j++)
            result->exprs[i] = Py_NewRef(x->exprs[j]);
        break;
    }
    return (PyObject *)result;
}

static PyObject *
bitvec_lshift(PyObject *a, PyObject *b)
{
    return bitvec_shift(a, b, SHIFT_SHL);
}

static PyObject *
bitvec_rshift(PyObject *a, PyObject *b)
{
    return bitvec_shift(a, b, SHIFT_SAR);
}

static PyObject *
bitvec_richcompare(PyObject *self, PyObject *other, int op)
{
    if (op != Py_EQ)
        Py_RETURN_NOTIMPLEMENTED;
    return constraint_create(&BitVecConstraint_Type, self, other);
}

static PyObject *
bitvec_repr(BitVecObject *self)
{
    PyObject *tuple, *result;
    Py_ssize_t size, i;

    size = Py_SIZE(self);
    tuple = PyTuple_New(size);
    if (tuple == NULL)
        return NULL;

    for (i = 0; i < size; i++)
        PyTuple_SET_ITEM(tuple, i, Py_NewRef(self->exprs[i]));
    result = PyObject_Repr(tuple);
    Py_DECREF(tuple);
    return result;
}

static Py_ssize_t
bitvec_length(BitVecObject *self)
{
    return Py_SIZE(self);
}

static PyObject *
bitvec_item(BitVecObject *self, Py_ssize_t i)
{
    if (i < 0 || i >= Py_SIZE(self)) {
        PyErr_SetString(PyExc_IndexError, "BitVec index out of range");
        return NULL;
    }
    return Py_NewRef(self->exprs[i]);
}

static void
bitvec_dealloc(BitVecObject *self)
{
    Py_ssize_t i;

    for (i = 0; i < Py_SIZE(self); i++)
        Py_XDECREF(self->exprs[i]);
    Py_XDECREF(self->system);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyNumberMethods bitvec_as_number = {
    .nb_invert = (unaryfunc)bitvec_invert,
    .nb_lshift = (binaryfunc)bitvec_lshift,
    .nb_rshift = (binaryfunc)bitvec_rshift,
    .nb_and = (binaryfunc)bitvec_and,
    .nb_xor = (binaryfunc)bitvec_xor,
    .nb_or = (binaryfunc)bitvec_or,
};

static PySequenceMethods bitvec_as_sequence = {
    .sq_length = (lenfunc)bitvec_length,
    .sq_item = (ssizeargfunc)bitvec_item,
};

static PyTypeObject BitVec_Type = {
    .ob_base = PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "xorsat.BitVec",
    .tp_basicsize = sizeof(BitVecObject),
    .tp_itemsize = sizeof(PyObject *),
    .tp_dealloc = (destructor)bitvec_dealloc,
    .tp_repr = (reprfunc)bitvec_repr,
    .tp_as_number = &bitvec_as_number,
    .tp_as_sequence = &bitvec_as_sequence,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_doc = NULL,
    .tp_richcompare = (richcmpfunc)bitvec_richcompare,
    .tp_new = bitvec_new,
};

/* =============================== Constraint =============================== */

PyObject *
constraint_create(PyTypeObject *type, PyObject *lhs, PyObject *rhs)
{
    ConstraintObject *self;

    self = (ConstraintObject *)type->tp_alloc(type, 0);
    if (self == NULL)
        return NULL;
    self->lhs = Py_NewRef(lhs);
    self->rhs = Py_NewRef(rhs);
    return (PyObject *)self;
}

static PyObject *
constraint_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *lhs, *rhs;

    if (!_PyArg_NoKeywords(type->tp_name, kwds))
        return NULL;
    if (!PyArg_ParseTuple(args, "OO", &lhs, &rhs))
        return NULL;

    if (!BitExpr_Check(lhs) && !BitExpr_Check(rhs)) {
        PyErr_SetString(PyExc_TypeError,
            "at least one of the arguments must be a BitExpr");
        return NULL;
    }
    return constraint_create(type, lhs, rhs);
}

static PyObject *
constraint_zeros(ConstraintObject *self)
{
    PyObject *zero, *result;

    zero = bitexpr_xor(self->lhs, self->rhs);
    if (zero == NULL)
        return NULL;
    result = Py_BuildValue("[O]", zero);
    Py_DECREF(zero);
    return result;
}

static void
constraint_dealloc(ConstraintObject *self)
{
    Py_XDECREF(self->lhs);
    Py_XDECREF(self->rhs);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyMethodDef constraint_methods[] = {
    { "zeros", (PyCFunction)constraint_zeros, METH_NOARGS, NULL },
    { NULL },
};

static PyMemberDef constraint_members[] = {
    { "lhs", T_OBJECT, offsetof(ConstraintObject, lhs), READONLY, NULL },
    { "rhs", T_OBJECT, offsetof(ConstraintObject, rhs), READONLY, NULL },
    { NULL },
};

static PyTypeObject Constraint_Type = {
    .ob_base = PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "xorsat.Constraint",
    .tp_basicsize = sizeof(ConstraintObject),
    .tp_itemsize = 0,
    .tp_dealloc = (destructor)constraint_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_doc = NULL,
    .tp_methods = constraint_methods,
    .tp_members = constraint_members,
    .tp_new = constraint_new,
};

/* ============================ BitVecConstraint ============================ */

static PyObject *
bitvecconstraint_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *lhs, *rhs;

    if (!_PyArg_NoKeywords(type->tp_name, kwds))
        return NULL;
    if (!PyArg_ParseTuple(args, "OO", &lhs, &rhs))
        return NULL;

    if (!BitVec_Check(lhs) && !BitVec_Check(rhs)) {
        PyErr_SetString(PyExc_TypeError,
            "at least one of the arguments must be a BitVec");
        return NULL;
    }
    return constraint_create(type, lhs, rhs);
}

static PyObject *
bitvecconstraint_zeros(BitVecConstraintObject *self)
{
    PyObject *zeros, *result;

    zeros = bitvec_xor(self->super.lhs, self->super.rhs);
    if (zeros == NULL)
        return zeros;
    result = PySequence_List(zeros);
    Py_DECREF(zeros);
    return result;
}

static PyMethodDef bitvecconstraint_methods[] = {
    { "zeros", (PyCFunction)bitvecconstraint_zeros, METH_NOARGS, NULL },
    { NULL },
};

static PyTypeObject BitVecConstraint_Type = {
    .ob_base = PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "xorsat.BitVecConstraint",
    .tp_basicsize = sizeof(BitVecConstraintObject),
    .tp_itemsize = 0,
    .tp_dealloc = (destructor)constraint_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_doc = NULL,
    .tp_methods = bitvecconstraint_methods,
    .tp_members = constraint_members,
    .tp_base = &Constraint_Type,
    .tp_new = bitvecconstraint_new,
};

/* ============================== LinearSystem ============================== */

static PyObject *
linearsystem_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    LinearSystemObject *self;
    PyObject *key, *value;
    Py_ssize_t bits, pos, offset, i;

    if (!PyArg_UnpackTuple(args, type->tp_name, 0, 0))
        return NULL;
    if (kwds == NULL) {
        PyErr_SetString(PyExc_TypeError, "expected at least one positional argument");
        return NULL;
    }
    if (!PyArg_ValidateKeywordArguments(kwds))
        return NULL;

    self = PyObject_GC_New(LinearSystemObject, type);
    if (self == NULL)
        return NULL;

    self->vi_size = PyDict_Size(kwds);
    self->vi_table = PyMem_Calloc(1, self->vi_size * sizeof(PyObject *));
    if (self->vi_table == NULL) {
        PyErr_NoMemory();
        goto error;
    }

    pos = offset = 0;
    for (i = 0; PyDict_Next(kwds, &pos, &key, &value); i++) {
        bits = PyNumber_AsSsize_t(value, PyExc_OverflowError);
        if (bits == -1 && PyErr_Occurred())
            goto error;
        if (bits <= 0) {
            PyErr_SetString(PyExc_ValueError, "number of bits must be positive");
            goto error;
        }
        self->vi_table[i] = varinfo_create(&VarInfo_Type, key, bits, offset);
        if (self->vi_table[i] == NULL)
            goto error;
        offset += bits;
    }
    self->bits = offset;

    // Cache 0 and 1 for efficiency
    self->_expr_const[0] = self->_expr_const[1] = NULL;
    for (i = 0; i < 2; i++) {
        self->_expr_const[i] = bitexpr_from_bit(&BitExpr_Type, i, self);
        if (self->_expr_const[i] == NULL)
            goto error;
    }

    PyObject_GC_Track(self);
    return (PyObject *)self;

error:
    Py_DECREF(self);
    return NULL;
}

PyObject *
linearsystem_gen_index(LinearSystemObject *self, Py_ssize_t index)
{
    BitVecObject *vec;
    BitExprObject *expr;
    VarInfoObject *var;
    Py_ssize_t i;

    var = (VarInfoObject *)self->vi_table[index];
    vec = (BitVecObject *)bitvec_from_size(&BitVec_Type, var->bits, (PyObject *)self, 0);
    if (vec == NULL)
        return NULL;
    for (i = 0; i < var->bits; i++) {
        expr = (BitExprObject *)bitexpr_from_bit(&BitExpr_Type, 0, self);
        if (expr == NULL) {
            Py_DECREF(vec);
            return NULL;
        }
        bitset_set((BitSetObject *)expr->mask, var->offset + i, 1);
        vec->exprs[i] = (PyObject *)expr;
    }
    return (PyObject *)vec;
}

static PyObject *
linearsystem_gen(LinearSystemObject *self, PyObject *index)
{
    Py_ssize_t i = PyNumber_AsSsize_t(index, PyExc_IndexError);
    if (i == -1 && PyErr_Occurred())
        return NULL;
    if (i < 0 || i >= self->vi_size) {
        PyErr_SetString(PyExc_IndexError, "index out of range");
        return NULL;
    }
    return linearsystem_gen_index(self, i);
}

static PyObject *
linearsystem_gens(LinearSystemObject *self)
{
    PyObject *result, *vec;
    Py_ssize_t i;

    result = PyList_New(self->vi_size);
    if (result == NULL)
        return NULL;

    for (i = 0; i < self->vi_size; i++) {
        vec = linearsystem_gen_index(self, i);
        if (vec == NULL) {
            Py_DECREF(vec);
            return NULL;
        }
        PyList_SET_ITEM(result, i, vec);
    }
    return result;
}

static PyObject *
linearsystem_variables(LinearSystemObject *self)
{
    PyObject *result;
    Py_ssize_t i;

    result = PyList_New(self->vi_size);
    if (result == NULL)
        return NULL;
    for (i = 0; i < self->vi_size; i++)
        PyList_SET_ITEM(result, i, Py_NewRef(self->vi_table[i]));
    return (PyObject *)result;
}

static PyObject *
linearsystem_repr(LinearSystemObject *self)
{
    return PyUnicode_FromFormat("Linear System with %zd variables and %zd bits",
        self->vi_size, self->bits);
}

static int
linearsystem_traverse(LinearSystemObject *self, visitproc visit, void *arg)
{
    Py_VISIT(self->_expr_const[0]);
    Py_VISIT(self->_expr_const[1]);
    return 0;
}

static int
linearsystem_clear(LinearSystemObject *self)
{
    Py_CLEAR(self->_expr_const[0]);
    Py_CLEAR(self->_expr_const[1]);
    return 0;
}

static void
linearsystem_dealloc(LinearSystemObject *self)
{
    Py_ssize_t i;

    PyObject_GC_UnTrack(self);
    linearsystem_clear(self);
    for (i = 0; i < self->vi_size; i++)
        Py_XDECREF(self->vi_table[i]);
    PyMem_Free(self->vi_table);
    PyObject_GC_Del(self);
}

static PyMethodDef linearsystem_methods[] = {
    { "gen", (PyCFunction)linearsystem_gen, METH_O, NULL },
    { "gens", (PyCFunction)linearsystem_gens, METH_NOARGS, NULL },
    { "variables", (PyCFunction)linearsystem_variables, METH_NOARGS, NULL },
    { NULL },
};

static PyTypeObject LinearSystem_Type = {
    .ob_base = PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "xorsat.LinearSystem",
    .tp_basicsize = sizeof(LinearSystemObject),
    .tp_itemsize = 0,
    .tp_dealloc = (destructor)linearsystem_dealloc,
    .tp_repr = (reprfunc)linearsystem_repr,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC,
    .tp_doc = NULL,
    .tp_traverse = (traverseproc)linearsystem_traverse,
    .tp_clear = (inquiry)linearsystem_clear,
    .tp_methods = linearsystem_methods,
    .tp_new = linearsystem_new,
};

/* ============================= solve_iterator ============================= */

static PyObject *
solveiter_next(SolveIterObject *it)
{
    // TODO: use gray code for more efficient enumeration

    PyObject *model;
    mzd_t *result;
    rci_t n, r;
    int sentinel;

    n = it->kernel->nrows;
    if (it->state[n])
        return NULL;  /* StopIteration */

    result = mzd_copy(NULL, it->x);
    for (r = 0; r < n; r++)
        if (it->state[r])
            mzd_combine_even_in_place(result, 0, 0, it->kernel, r, 0);

    sentinel = 1;
    for (r = 0; r < n; r++) {
        if ((it->state[r] ^= 1)) {
            sentinel = 0;
            break;
        }
    }
    it->state[n] = sentinel;

    model = generate_model(result, (LinearSystemObject *)it->system);
    mzd_free(result);
    return model;
}

static int
solveiter_traverse(SolveIterObject *self, visitproc visit, void *arg)
{
    Py_VISIT(self->system);
    return 0;
}

static int
solveiter_clear(SolveIterObject *self)
{
    Py_CLEAR(self->system);
    return 0;
}

static void
solveiter_dealloc(SolveIterObject *self)
{
    PyObject_GC_UnTrack(self);
    solveiter_clear(self);
    mzd_free(self->x);
    mzd_free(self->kernel);
    PyMem_Free(self->state);
    PyObject_GC_Del(self);
}

static PyTypeObject SolveIter_Type = {
    .ob_base = PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "xorsat.solve_iterator",
    .tp_basicsize = sizeof(SolveIterObject),
    .tp_itemsize = 0,
    .tp_dealloc = (destructor)solveiter_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_doc = NULL,
    .tp_traverse = (traverseproc)solveiter_traverse,
    .tp_clear = (inquiry)solveiter_clear,
    .tp_iter = PyObject_SelfIter,
    .tp_iternext = (iternextfunc)solveiter_next,
};

/* =========================== Module definitions =========================== */

static PyObject *
xorsat_lshr(PyObject *self, PyObject *const *args, Py_ssize_t nargs)
{
    if (!_PyArg_CheckPositional("LShR", nargs, 2, 2))
        return NULL;
    return bitvec_shift(args[0], args[1], SHIFT_SHR);
}

static PyObject *
xorsat_rotl(PyObject *self, PyObject *const *args, Py_ssize_t nargs)
{
    if (!_PyArg_CheckPositional("RotL", nargs, 2, 2))
        return NULL;
    return bitvec_shift(args[0], args[1], SHIFT_ROL);
}

static PyObject *
xorsat_rotr(PyObject *self, PyObject *const *args, Py_ssize_t nargs)
{
    if (!_PyArg_CheckPositional("RotR", nargs, 2, 2))
        return NULL;
    return bitvec_shift(args[0], args[1], SHIFT_ROR);
}

static PyObject *
xorsat_par(PyObject *self, PyObject *arg)
{
    BitVecObject *vec, *result;
    BitExprObject *expr, *acc;
    Py_ssize_t size, i;

    if (!BitVec_Check(arg)) {
        PyErr_SetString(PyExc_TypeError, "argument must be a BitVec");
        return NULL;
    }

    vec = (BitVecObject *)arg;
    size = Py_SIZE(vec);
    result = (BitVecObject *)bitvec_from_size(&BitVec_Type, size, vec->system, 1);
    if (result == NULL)
        return NULL;

    acc = (BitExprObject *)bitexpr_from_bit(&BitExpr_Type, 0,
        (LinearSystemObject *)vec->system);
    if (acc == NULL) {
        Py_DECREF(result);
        return NULL;
    }
    for (i = 0; i < size; i++) {
        expr = (BitExprObject *)vec->exprs[i];
        bitset_inplace_xor((BitSetObject *)acc->mask, (BitSetObject *)expr->mask);
        acc->compl ^= expr->compl;
    }
    Py_SETREF(result->exprs[0], (PyObject *)acc);
    return (PyObject *)result;
}

static PyObject *
xorsat_broadcast(PyObject *self, PyObject *args)
{
    BitVecObject *vec, *result;
    Py_ssize_t index = 0, size, i;

    if (!PyArg_ParseTuple(args, "O!|n", &BitVec_Type, &vec, &index))
        return NULL;
    size = Py_SIZE(vec);
    if (index < 0 || index >= size) {
        PyErr_SetString(PyExc_IndexError, "index out of range");
        return NULL;
    }

    result = (BitVecObject *)bitvec_from_size(&BitVec_Type, size, vec->system, 0);
    if (result == NULL)
        return NULL;

    for (i = 0; i < size; i++)
        result->exprs[i] = Py_NewRef(vec->exprs[index]);
    return (PyObject *)result;
}

PyObject *
generate_model(mzd_t *x, LinearSystemObject *system)
{
    VarInfoObject *var;
    PyObject *result, *one = NULL, *num = NULL, *bit = NULL;
    Py_ssize_t i, b;

    assert(x->nrows == 1);

    result = PyDict_New();
    if (result == NULL)
        return NULL;

    one = PyLong_FromLong(1);
    if (one == NULL)
        goto error;

    for (i = 0; i < system->vi_size; i++) {
        var = (VarInfoObject *)system->vi_table[i];
        num = PyLong_FromLong(0);
        if (num == NULL)
            goto error;
        for (b = var->offset + var->bits - 1; b >= var->offset; b--) {
            num = PyNumber_Lshift(num, one);
            bit = PyLong_FromLong(mzd_read_bit(x, 0, b));
            if (bit == NULL)
                goto error;
            num = PyNumber_Or(num, bit);
            Py_DECREF(bit);
        }
        if (PyDict_SetItem(result, var->name, num) < 0)
            goto error;
        Py_DECREF(num);
    }

    Py_DECREF(one);
    return result;

error:
    Py_DECREF(result);
    Py_XDECREF(one);
    Py_XDECREF(num);
    Py_XDECREF(bit);
    return NULL;
}

static PyObject *
xorsat__solve_zeros(PyObject *self, PyObject *args, PyObject *kwds)
{
    // Gaussian elimination algorithm based on:
    // https://github.com/nneonneo/pwn-stuff/blob/main/math/gf2.py

    static char *kwlist[] = { "", "all", NULL };
    LinearSystemObject *system;
    BitExprObject *expr, *expr2;
    BitSetObject *mask;
    SolveIterObject *it = NULL;
    PyObject *constraints, **items, *seq = NULL;
    PyObject *model;
    Py_ssize_t size, i;
    rci_t rows, cols, r, c, *leads;
    mzd_t *M = NULL, *x = NULL, *window, *kernel, *kernel_trans = NULL;
    int all = 0, dot;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|p", kwlist, &constraints, &all))
        return NULL;

    seq = PySequence_Fast(constraints, "argument is not iterable");
    if (seq == NULL)
        return NULL;

    size = PySequence_Fast_GET_SIZE(seq);
    if (size == 0) {
        PyErr_SetString(PyExc_ValueError, "argument must contain at least one equation");
        goto error;
    }
    if (size >= INT_MAX) {
        PyErr_SetString(PyExc_OverflowError, "number of equations must be <2^31");
        goto error;
    }

    items = PySequence_Fast_ITEMS(seq);
    for (i = 0; i < size; i++) {
        if (!BitExpr_Check(items[i])) {
            PyErr_Format(PyExc_TypeError,
                "expected iterable of BitExprs, got: '%.200s'",
                Py_TYPE(items[i])->tp_name);
            goto error;
        }
    }

    expr = (BitExprObject *)items[0];
    for (i = 1; i < size; i++) {
        expr2 = (BitExprObject *)items[i];
        if (!Py_Is(expr->system, expr2->system)) {
            PyErr_SetString(PyExc_TypeError,
                "iterable cannot contain differing linear systems");
            goto error;
        }
    }

    system = (LinearSystemObject *)expr->system;
    if (system->bits >= INT_MAX - 1) {
        PyErr_SetString(PyExc_OverflowError, "number of bits in system must be <2^31-1");
        goto error;
    }

    rows = (rci_t)size;
    cols = (rci_t)system->bits;
    M = mzd_init(rows, cols + 1);

    for (r = 0; r < rows; r++) {
        expr = (BitExprObject *)items[r];
        mask = (BitSetObject *)expr->mask;
        memcpy(mzd_row(M, r), mask->buf, (cols + 7) / 8);
        mzd_write_bit(M, r, cols, expr->compl);
    }

    // Reduce the augmented matrix to row echelon form (but not fully reduced)
    mzd_echelonize(M, 0);

    // Test if there is no solution (A[r] == [0 0 0 ... 0 0 0 1])
    for (r = 0; r < rows; r++) {
        window = mzd_init_window(M, r, 0, r + 1, cols);
        if (mzd_is_zero(window) && mzd_read_bit(M, r, cols) == 1) {
            PyErr_SetString(PyExc_ValueError, "no solution");
            goto error;
        }
        mzd_free_window(window);
    }

    leads = (rci_t *)PyMem_Malloc(rows * sizeof(rci_t));
    if (leads == NULL) {
        PyErr_NoMemory();
        goto error;
    }

    for (r = c = 0; r < rows; r++) {
        while (c < cols && !mzd_read_bit(M, r, c)) ++c;
        if (c == cols) break;
        leads[r] = c;
    }

    // Find a solution via back-substitution
    x = mzd_init(1, cols);
    for (; r--; ) {
        c = leads[r];
        if (c == -1)
            continue;

        // TODO: improve speed
        dot = 0;
        for (i = c + 1; i < cols; i++)
            dot ^= mzd_read_bit(M, r, i) & mzd_read_bit(x, 0, i);

        // x[c] = M[r,c+1:cols] * x[c+1:cols] + M[r,cols]
        mzd_write_bit(x, 0, c, dot ^ mzd_read_bit(M, r, cols));
    }

    PyMem_Free(leads);

    if (all) {
        window = mzd_init_window(M, 0, 0, rows, cols);
        kernel = mzd_kernel_left_pluq(window, 0);
        if (kernel != NULL) {
            kernel_trans = mzd_transpose(NULL, kernel);
            mzd_free(kernel);
        } else {
            kernel_trans = mzd_init(0, 0);
        }
        mzd_free_window(window);

        it = PyObject_GC_New(SolveIterObject, &SolveIter_Type);
        if (it == NULL)
            goto error;

        it->system = Py_NewRef(system);
        it->x = x;
        it->kernel = kernel_trans;
        it->state = PyMem_Calloc(1, kernel_trans->nrows + 1);
        if (it->state == NULL) {
            PyErr_NoMemory();
            goto error;
        }

        Py_DECREF(seq);
        mzd_free(M);
        return (PyObject *)it;
    }

    model = generate_model(x, system);
    Py_DECREF(seq);
    mzd_free(M);
    mzd_free(x);
    return model;

error:
    Py_XDECREF(seq);
    Py_XDECREF(it);
    mzd_xfree(M);
    mzd_xfree(x);
    mzd_xfree(kernel_trans);
    return NULL;
}

static PyMethodDef xorsat_methods[] = {
    { "LShR", (_PyCFunctionFast)xorsat_lshr, METH_FASTCALL, NULL },
    { "RotL", (_PyCFunctionFast)xorsat_rotl, METH_FASTCALL, NULL },
    { "RotR", (_PyCFunctionFast)xorsat_rotr, METH_FASTCALL, NULL },
    { "Par", (PyCFunction)xorsat_par, METH_O, NULL },
    { "Broadcast", (PyCFunction)xorsat_broadcast, METH_VARARGS, NULL },
    { "_solve_zeros", (PyCFunction)xorsat__solve_zeros, METH_VARARGS, NULL },
    { NULL },
};

static PyModuleDef _xorsatmodule = {
    .m_base = PyModuleDef_HEAD_INIT,
    .m_name = "_xorsat",
    .m_doc = NULL,
    .m_size = -1,
    .m_methods = xorsat_methods,
};

PyMODINIT_FUNC
PyInit__xorsat(void)
{
    PyObject *mod;

#define INIT_TYPE(type) {         \
    if (PyType_Ready(&type) < 0)  \
        return NULL;              \
}

    INIT_TYPE(BitExpr_Type);
    INIT_TYPE(BitRef_Type);
    INIT_TYPE(BitSet_Type);
    INIT_TYPE(BitVec_Type);
    INIT_TYPE(BitVecConstraint_Type);
    INIT_TYPE(Constraint_Type);
    INIT_TYPE(LinearSystem_Type);
    INIT_TYPE(SolveIter_Type);
    INIT_TYPE(VarInfo_Type);

    mod = PyModule_Create(&_xorsatmodule);
    if (mod == NULL)
        return NULL;

#define ADD_TYPE(type) {                     \
    if (PyModule_AddType(mod, &type) < 0) {  \
        Py_DECREF(mod);                      \
        return NULL;                         \
    }                                        \
}

    ADD_TYPE(BitExpr_Type);
    ADD_TYPE(BitSet_Type);
    ADD_TYPE(BitVec_Type);
    ADD_TYPE(LinearSystem_Type);

    return mod;
}
