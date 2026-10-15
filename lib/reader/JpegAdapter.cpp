#include "PageDecoder.h"
#include "JobPump.h"
#include <JPEGDEC.h>
#include <climits>
#include <new>
namespace reader {
static SdCard *jpeg_card=nullptr;
static File jpeg_file;
static void *open_jpeg(const char *path,int32_t *size) {
    jpeg_file=jpeg_card->open(path);
    if (!jpeg_file || jpeg_file.size()>INT32_MAX) { jpeg_file.close(); return nullptr; }
    *size=jpeg_file.size(); return &jpeg_file;
}
static void close_jpeg(void *handle) { static_cast<File *>(handle)->close(); }
static int32_t read_jpeg(JPEGFILE *file,uint8_t *bytes,int32_t length) {
    return pump_job()?static_cast<File *>(file->fHandle)->read(bytes,length):0;
}
static int32_t seek_jpeg(JPEGFILE *file,int32_t position) {
    auto *f=static_cast<File *>(file->fHandle); return position>=0 && f->seek(position)?int32_t(f->position()):-1;
}
static int draw_jpeg(JPEGDRAW *draw) {
    auto *output=static_cast<PageDecoder *>(draw->pUser);
    return output->block(draw->x,draw->y,draw->iWidthUsed,draw->iHeight,draw->iWidth,draw->pPixels)?1:0;
}
bool decode_jpeg(PageDecoder &output,const std::string &path,std::string &error) {
    auto *memory=heap_caps_malloc(sizeof(JPEGDEC),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!memory) { error="Not enough PSRAM for JPEG decoder."; return false; }
    auto *jpeg=new(memory) JPEGDEC(); jpeg_card=&output.storage();
    bool opened=jpeg->open(path.c_str(),open_jpeg,close_jpeg,read_jpeg,seek_jpeg,draw_jpeg);
    bool ok=false;
    if (!opened) error="Cannot decode this JPEG page.";
    else if (jpeg->getJPEGType()!=JPEG_MODE_BASELINE) error="Progressive JPEG is not supported yet.";
    else {
        const int w=jpeg->getWidth(),h=jpeg->getHeight(); auto fit=fit_page(w,h);
        int divisor=1,flags=0;
        if (w/8>=fit.width && h/8>=fit.height) { divisor=8;flags=JPEG_SCALE_EIGHTH; }
        else if (w/4>=fit.width && h/4>=fit.height) { divisor=4;flags=JPEG_SCALE_QUARTER; }
        else if (w/2>=fit.width && h/2>=fit.height) { divisor=2;flags=JPEG_SCALE_HALF; }
        if (!output.prepare(w,h,(w+divisor-1)/divisor,(h+divisor-1)/divisor)) error="JPEG dimensions exceed 4096 x 8192.";
        else {
            jpeg->setUserPointer(&output); jpeg->setPixelType(RGB565_LITTLE_ENDIAN);
            ok=jpeg->decode(0,0,flags)!=0;
            if (!ok) error="JPEG decoding failed or was cancelled.";
        }
    }
    if (opened) jpeg->close();
    jpeg_file.close(); jpeg_card=nullptr; jpeg->~JPEGDEC(); heap_caps_free(memory); return ok;
}
}
