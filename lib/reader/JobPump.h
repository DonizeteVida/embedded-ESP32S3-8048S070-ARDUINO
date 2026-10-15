#pragma once
namespace reader {
using Pump = bool (*)(void *);
void set_job_pump(Pump pump, void *context);
bool pump_job();
}
