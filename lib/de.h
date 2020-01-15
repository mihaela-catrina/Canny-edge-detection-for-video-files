#include <stdint.h>

#include <libavformat/avformat.h>

typedef struct {
    AVFrame *frame;
    uint8_t *data;
    int      width;
    int      height;
} DeFrame;

typedef struct {
    AVFormatContext     *fmt_ctx;

    AVCodecContext      *decoder_ctx;
    AVCodecContext      *encoder_ctx;

    struct SwsContext   *sws_ctx;
    struct SwsContext   *encoder_sws_ctx;

    AVFrame             *encoded_frame;
    int                  stream_id;
    FILE                *outfile;

    int                  frame_count;
    int                  got_last;
} DeContext;


void        de_context_free             (DeContext *context);
DeContext  *de_context_create           (const char *infile);
DeFrame    *de_context_get_next_frame   (DeContext *context, int *got_frame);
void        de_context_prepare_encoding (DeContext *context, const char *outfile);
void        de_context_set_next_frame   (DeContext *context, DeFrame *frame);
void        de_context_end_encoding     (DeContext *context);
