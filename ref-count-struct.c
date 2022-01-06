#include "ref-count-struct.h"

#include <glib.h>

void util_ref_counted_struct_init(UtilRefCountedBase *base)
{
    base->refcount = 1;
}

gpointer util_ref_counted_struct_ref(UtilRefCountedBase *base)
{
    if (base != NULL)
    {
        g_atomic_int_inc(&base->refcount);
    }
    return base;
}

gboolean util_ref_counted_struct_unref(UtilRefCountedBase *base)
{
    if (base != NULL)
    {
        return g_atomic_int_dec_and_test(&base->refcount);
    }
    else
    {
        return FALSE;
    }
}

void util_ref_counted_struct_unref_full(UtilRefCountedBase *base, GDestroyNotify destructor)
{
    if (util_ref_counted_struct_unref(base))
    {
        destructor(base);
    }
}
