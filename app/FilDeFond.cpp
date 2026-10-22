// Prométhée : priorité des calculs en fond. Licence GPL-3.0-only.
#include "FilDeFond.hpp"

#if defined(_WIN32)
#include <windows.h>
void prioriteDeFond() { SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL); }
#elif defined(__linux__)
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
// Sous Linux, la gentillesse est propre à chaque fil et passe aux fils qu'il crée
void prioriteDeFond() { setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), 10); }
#elif defined(__APPLE__)
#include <pthread.h>
#include <sys/qos.h>
void prioriteDeFond() { pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0); }
#else
void prioriteDeFond() {}
#endif
