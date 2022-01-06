#include "zip-input-stream.h"

#include <glib.h>

struct _DtZipInputStream
{
    GInputStream parent_instance;

    GMutex mutex;
    zip_t *zip;
    zip_file_t *member;

    DtZipInputStreamCloseCallback close_callback;
    gpointer close_callback_data;
};


G_DEFINE_TYPE(DtZipInputStream, dt_zip_input_stream, G_TYPE_INPUT_STREAM);

GQuark dt_libzip_error_quark(void)
{
    return g_quark_from_static_string("dt-libzip-error-quark");
}

static void dt_zip_input_stream_dispose(GObject *gobj);
static void dt_zip_input_stream_finalize(GObject *gobj);

static gssize dt_zip_input_stream_read_fn(GInputStream *stream, void *buffer, gsize count, GCancellable *cancellable, GError **error);
static gboolean dt_zip_input_stream_close_fn(GInputStream *stream, GCancellable *cancellable, GError **error);

static void dt_zip_input_stream_dispose(GObject *gobj)
{
    // Chain up to GObject.
    g_debug("dt_zip_input_stream_dispose called\n");
    G_OBJECT_CLASS(dt_zip_input_stream_parent_class)->dispose(gobj);
}

static void dt_zip_input_stream_finalize(GObject *gobj)
{
    // Free anything that we didn't free in dispose().
    DtZipInputStream *self = DT_ZIP_INPUT_STREAM(gobj);

    g_debug("dt_zip_input_stream_finalize called\n");
    g_mutex_lock(&self->mutex);
    if (self->member != NULL)
    {
        zip_fclose(self->member);
    }
    if (self->zip != NULL)
    {
        if (self->close_callback != NULL)
        {
            //zip_discard(self->zip);
            self->close_callback(self, self->zip, self->close_callback_data);
        }
        self->zip = NULL;
    }
    g_mutex_unlock(&self->mutex);
    g_mutex_clear(&self->mutex);

    G_OBJECT_CLASS(dt_zip_input_stream_parent_class)->finalize(gobj);
}

static void dt_zip_input_stream_default_close_callback(DtZipInputStream *stream, zip_t *zip, gpointer data)
{
    zip_discard(zip);
}

static void dt_zip_input_stream_init(DtZipInputStream *self)
{
    g_mutex_init(&self->mutex);
    self->close_callback = dt_zip_input_stream_default_close_callback;
    self->zip = NULL;
    self->member = NULL;
}

static void dt_zip_input_stream_class_init(DtZipInputStreamClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GInputStreamClass *stream_class = G_INPUT_STREAM_CLASS(klass);

    object_class->dispose = dt_zip_input_stream_dispose;
    object_class->finalize = dt_zip_input_stream_finalize;

    stream_class->read_fn  = dt_zip_input_stream_read_fn;
    stream_class->close_fn = dt_zip_input_stream_close_fn;
    // The default implementation of skip just works by calling read_fn, which
    // is good enough. That's all we'd be able to do anyway.
    //stream_class->skip     = dt_zip_input_stream_skip;
}

static void setZipError(GError **error, zip_error_t *ze)
{
    if (error != NULL)
    {
        int zipErr = zip_error_code_zip(ze);
        // The zip_error_code_system returns either a zlib error or an errno
        // value. Just ignore it for now.
        //int sysErr = zip_error_code_system(ze);
        const char *str = zip_error_strerror(ze);
        g_set_error(error, DT_LIBZIP_ERROR, zipErr,
                "libzip error: %s", str);
    }
}

static gssize dt_zip_input_stream_read_fn(GInputStream *stream, void *buffer,
        gsize count, GCancellable *cancellable, GError **error)
{
    DtZipInputStream *self = DT_ZIP_INPUT_STREAM(stream);
    zip_int64_t num;
    g_mutex_lock(&self->mutex);

    num = zip_fread(self->member, buffer, count);
    if (num < 0)
    {
        setZipError(error, zip_file_get_error(self->member));
    }
    g_mutex_unlock(&self->mutex);

    return num;
}

static gboolean dt_zip_input_stream_close_fn(GInputStream *stream, GCancellable *cancellable, GError **error)
{
    DtZipInputStream *self = DT_ZIP_INPUT_STREAM(stream);
    g_mutex_lock(&self->mutex);
    if (self->member != NULL)
    {
        zip_fclose(self->member);
        self->member = NULL;
    }
    if (self->zip != NULL)
    {
        if (self->close_callback != NULL)
        {
            self->close_callback(self, self->zip, self->close_callback_data);
        }
        //zip_discard(self->zip);
        self->zip = NULL;
    }
    g_mutex_unlock(&self->mutex);
    return TRUE;
}

DtZipInputStream *dt_zip_input_stream_new(zip_t *zip, zip_file_t *member)
{
    DtZipInputStream *self = g_object_new(DT_TYPE_ZIP_INPUT_STREAM, NULL);
    self->zip = zip;
    self->member = member;

    return self;
}

void dt_zip_input_stream_set_close_callback(DtZipInputStream *self, DtZipInputStreamCloseCallback callback, gpointer data)
{
    if (callback != NULL)
    {
        self->close_callback = callback;
        self->close_callback_data = data;
    }
    else
    {
        self->close_callback = dt_zip_input_stream_default_close_callback;
        self->close_callback_data = NULL;
    }
}

zip_t *dt_zip_input_stream_get_zip(DtZipInputStream *self)
{
    return self->zip;
}

zip_file_t *dt_zip_input_stream_get_member(DtZipInputStream *self)
{
    return self->member;
}

