/* A mod runs on one thread: rcheevos's mutexes need do nothing. */
#ifndef MOD_PTHREAD_H
#define MOD_PTHREAD_H

typedef int pthread_mutex_t;
typedef int pthread_mutexattr_t;
#define PTHREAD_MUTEX_RECURSIVE 1

static inline int pthread_mutexattr_init(pthread_mutexattr_t *a) { (void)a; return 0; }
static inline int pthread_mutexattr_destroy(pthread_mutexattr_t *a) { (void)a; return 0; }
static inline int pthread_mutexattr_settype(pthread_mutexattr_t *a, int t) { (void)a; (void)t; return 0; }
static inline int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a) { (void)m; (void)a; return 0; }
static inline int pthread_mutex_destroy(pthread_mutex_t *m) { (void)m; return 0; }
static inline int pthread_mutex_lock(pthread_mutex_t *m) { (void)m; return 0; }
static inline int pthread_mutex_unlock(pthread_mutex_t *m) { (void)m; return 0; }

#endif
