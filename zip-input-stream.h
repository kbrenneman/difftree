#ifndef ZIP_INPUT_STREAM_H
#define ZIP_INPUT_STREAM_H

/**
 * A GInputStream subclass that reads an entry from a zip file.
 *
 * This class just uses libzip, and doesn't do anything special for the async
 * functions, so it uses it the thread-based defaults from GInputStream.
 *
 * Since libzip isn't thread-safe, that means that nothing else can use the
 * zip_t object at the same time.
 */

#include <gio/gio.h>
#include <zip.h>

G_BEGIN_DECLS

GQuark dt_libzip_error_quark(void);
#define DT_LIBZIP_ERROR dt_libzip_error_quark()

#define DT_TYPE_ZIP_INPUT_STREAM dt_zip_input_stream_get_type()
G_DECLARE_FINAL_TYPE(DtZipInputStream, dt_zip_input_stream, DT, ZIP_INPUT_STREAM, GInputStream);

typedef void (* DtZipInputStreamCloseCallback) (DtZipInputStream *stream, zip_t *zip, gpointer data);

DtZipInputStream *dt_zip_input_stream_new(zip_t *zip, zip_file_t *member);

/**
 * Sets a callback function which is used to close the zip file.
 *
 * If no callback is set, then it will simply call zip_discard.
 */
void dt_zip_input_stream_set_close_callback(DtZipInputStream *self, DtZipInputStreamCloseCallback callback, gpointer data);

zip_t *dt_zip_input_stream_get_zip(DtZipInputStream *self);
zip_file_t *dt_zip_input_stream_get_member(DtZipInputStream *self);

G_END_DECLS

#endif // ZIP_INPUT_STREAM_H
