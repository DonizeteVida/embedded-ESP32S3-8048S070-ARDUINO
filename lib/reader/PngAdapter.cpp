// This translation unit matches the per-library build flags in codec_build.py.
#define Z_PREFIX
#define z_errmsg png_z_errmsg
#include <PNGdec.h>
#include "PageDecoder.h"
#include "JobPump.h"
#include <climits>
#include <new>
namespace reader {
static SdCard *png_card=nullptr;
static File png_file;
struct PngOutput { PNG *png; PageDecoder *output; uint16_t *row; bool stopped=false; };
static void *open_png(const char *path,int32_t *size) {
    png_file=png_card->open(path);
    if (!png_file || png_file.size()>INT32_MAX) { png_file.close(); return nullptr; }
    *size=png_file.size();return &png_file;
}
static void close_png(void *handle) { static_cast<File *>(handle)->close(); }
static int32_t read_png(PNGFILE *file,uint8_t *bytes,int32_t length) {
    return pump_job()?static_cast<File *>(file->fHandle)->read(bytes,length):0;
}
static int32_t seek_png(PNGFILE *file,int32_t position) {
    auto *f=static_cast<File *>(file->fHandle);return position>=0 && f->seek(position)?int32_t(f->position()):-1;
}
static int draw_png(PNGDRAW *draw) {
    auto *context=static_cast<PngOutput *>(draw->pUser);
    context->png->getLineAsRGB565(draw,context->row,PNG_RGB565_LITTLE_ENDIAN,0xffffffff);
    if (!context->output->block(0,draw->y,draw->iWidth,1,draw->iWidth,context->row)) { context->stopped=true;return 0; }
    return 1;
}
bool decode_png(PageDecoder &output,const std::string &path,std::string &error) {
    auto *memory=heap_caps_malloc(sizeof(PNG),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    auto *row=static_cast<uint16_t *>(heap_caps_malloc(4096*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if (!memory || !row) { heap_caps_free(memory);heap_caps_free(row);error="Not enough PSRAM for PNG decoder.";return false; }
    auto *png=new(memory) PNG();png_card=&output.storage();
    bool opened=png->open(path.c_str(),open_png,close_png,read_png,seek_png,draw_png)==PNG_SUCCESS;
    bool ok=false;
    if (!opened) error="Cannot decode this PNG page.";
    else if (png->isInterlaced()) error="Interlaced PNG is not supported yet.";
    else if (!output.prepare(png->getWidth(),png->getHeight(),png->getWidth(),png->getHeight())) error="PNG dimensions exceed 4096 x 8192.";
    else {
        PngOutput context;context.png=png;context.output=&output;context.row=row;
        ok=png->decode(&context,PNG_CHECK_CRC)==PNG_SUCCESS && !context.stopped;
        if (!ok) error="PNG decoding failed or was cancelled.";
    }
    if (opened) png->close();
    png_file.close();png_card=nullptr;png->~PNG();heap_caps_free(memory);heap_caps_free(row);return ok;
}
}
