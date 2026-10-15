#include "PageDecoder.h"
#include "JobPump.h"
namespace reader {
PageFit fit_page(int w,int h,int cw,int ch) {
    PageFit fit;
    if (w<=0 || h<=0 || cw<=0 || ch<=0) return fit;
    if (int64_t(w)*ch<=int64_t(h)*cw) { fit.height=ch; fit.width=std::max<int>(1,int64_t(w)*ch/h); }
    else { fit.width=cw; fit.height=std::max<int>(1,int64_t(h)*cw/w); }
    fit.x=(cw-fit.width)/2; fit.y=(ch-fit.height)/2; return fit;
}
void PageDecoder::release() { heap_caps_free(front);heap_caps_free(staging);front=staging=nullptr; }
bool PageDecoder::prepare(int w,int h,int dw,int dh) {
    if (w<=0 || h<=0 || w>4096 || h>8192 || dw<=0 || dh<=0) return false;
    fit=fit_page(w,h); source_width=dw; source_height=dh; return true;
}
bool PageDecoder::block(int x,int y,int width,int height,int stride,const uint16_t *pixels) {
    if (!staging || !source_width || !source_height || !pixels || width<1 || height<1 || stride<width) return false;
    int first_x=std::max(0,(x*fit.width+source_width-1)/source_width);
    int end_x=std::min(fit.width,((x+width)*fit.width+source_width-1)/source_width);
    int first_y=std::max(0,(y*fit.height+source_height-1)/source_height);
    int end_y=std::min(fit.height,((y+height)*fit.height+source_height-1)/source_height);
    for (int dy=first_y;dy<end_y;++dy) {
        int sy=dy*source_height/fit.height-y;
        for (int dx=first_x;dx<end_x;++dx) {
            int sx=dx*source_width/fit.width-x;
            if (sx>=0 && sx<width && sy>=0 && sy<height) staging[(fit.y+dy)*480+fit.x+dx]=pixels[sy*stride+sx];
        }
    }
    return pump_job();
}
bool PageDecoder::decode(const Page &page) {
    failure.clear();
    if (!front) front=static_cast<uint16_t *>(heap_caps_malloc(480*800*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if (!staging) staging=static_cast<uint16_t *>(heap_caps_malloc(480*800*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if (!front || !staging) { failure="Not enough PSRAM for a page."; return false; }
    memset(staging,0,480*800*2);
    const std::string path=page.cache_path();
    bool ok=std::string(page.suffix)=="png"?decode_png(*this,path,failure):decode_jpeg(*this,path,failure);
    if (ok) std::swap(front,staging);
    return ok;
}
}
