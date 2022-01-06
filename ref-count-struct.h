#ifndef REF_COUNT_STRUCT_H
#define REF_COUNT_STRUCT_H

/**
 * \file
 *
 * Some helpers for defining reference-counted boxed types.
 *
 * This is an easy way to make a normal struct into a reference-counted object
 * with a usable GType, without having to implement a full GObject subclass.
 */

#include <glib-object.h>

G_BEGIN_DECLS

/**
 * Declares the functions that are defiend by UTIL_DEFINE_BOXED_REFCOUNT_TYPE.
 *
 * \param TypeName The type name of the struct.
 * \param type_name The type name in lower case.
 */
#define UTIL_DECLARE_BOXED_REFCOUNT_FUNCS(TypeName, type_name) \
    TypeName *type_name##_ref(TypeName *t); \
    void type_name##_unref(TypeName *t); \
    GType type_name##_get_type(void);

/**
 * This is a wrapper around G_DEFINE_BOXED_TYPE, which uses the given
 * destructor function.
 *
 * This defines functions with "_ref" and "_unref" suffixes. The
 * type_name_unref function will unref the struct, and call \p free_func
 * if the refcount reaches zero.
 *
 * The type_name_ref function is just a type-safe wrapper around
 * util_ref_counted_struct_ref.
 *
 * \param TypeName the type name of the struct.
 * \param type_name the type name in lower case.
 * \param free_func a function to destroy the struct.
 */
#define UTIL_DEFINE_BOXED_REFCOUNT_TYPE(TypeName, type_name, free_func) \
    TypeName *type_name##_ref(TypeName *t) \
    { \
        return (TypeName *) util_ref_counted_struct_ref((UtilRefCountedBase *) t); \
    } \
    void type_name##_unref(TypeName *t) \
    { \
        util_ref_counted_struct_unref_full((UtilRefCountedBase *) t, (GDestroyNotify) free_func); \
    } \
    G_DEFINE_BOXED_TYPE(TypeName, type_name, (GBoxedCopyFunc) util_ref_counted_struct_ref, (GBoxedFreeFunc) type_name##_unref)

/**
 * This should be the first element in the struct. It just keeps the reference
 * count.
 */
typedef struct
{
    gint refcount;
} UtilRefCountedBase;

void util_ref_counted_struct_init(UtilRefCountedBase *base);

/**
 * Increments the reference count of a UtilRefCountedBase struct.
 */
gpointer util_ref_counted_struct_ref(UtilRefCountedBase *base);

/**
 * Decrements the reference count of a UtilRefCountedBase struct.
 *
 * \return TRUE if \p base is not NULL and if the refcount reached zero.
 */
gboolean util_ref_counted_struct_unref(UtilRefCountedBase *base);

/**
 * Decrements the reference count of a UtilRefCountedBase struct, and calls
 * \p destructor if the reference count reached zero.
 */
void util_ref_counted_struct_unref_full(UtilRefCountedBase *base, GDestroyNotify destructor);

G_END_DECLS

#endif // REF_COUNT_STRUCT_H
