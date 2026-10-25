// Prométhée : priorité des calculs en fond. Licence GPL-3.0-only.
#include "FilDeFond.hpp"

#if defined(_WIN32)
#include <windows.h>
void prioriteDeFond() { SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL); }
void prioriteMinimale() { SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_IDLE); }
#elif defined(__linux__)
#include <sched.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
// Sous Linux, la gentillesse est propre à chaque fil et passe aux fils qu'il crée
void prioriteDeFond() { setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), 10); }
// SCHED_IDLE (permis sans droits particuliers) ; à défaut, la gentillesse la plus grande
void prioriteMinimale() {
  sched_param p{};
  if (sched_setscheduler(0, SCHED_IDLE, &p) != 0) setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), 19);
}
#elif defined(__APPLE__)
#include <pthread.h>
#include <sys/qos.h>
void prioriteDeFond() { pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0); }
void prioriteMinimale() { pthread_set_qos_class_self_np(QOS_CLASS_BACKGROUND, 0); }
#else
void prioriteDeFond() {}
void prioriteMinimale() {}
#endif
