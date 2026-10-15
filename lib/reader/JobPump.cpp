#include "JobPump.h"
namespace reader {
static Pump callback=nullptr;
static void *context=nullptr;
void set_job_pump(Pump pump,void *user) { callback=pump; context=user; }
bool pump_job() { return !callback || callback(context); }
}
