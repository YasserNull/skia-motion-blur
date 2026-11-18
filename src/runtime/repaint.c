#include "repaint.h"
#include "policy.h"
#include <android/binder_ibinder.h>
#include <android/binder_parcel.h>
#include "log.h"
#include <dlfcn.h>
#include <pthread.h>
#include <stdatomic.h>

static pthread_once_t worker_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t worker_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static uint64_t pending;
static atomic_bool ready;

static void *on_create(void *value) { return value; }
static void on_destroy(void *value) { (void)value; }
static binder_status_t on_transact(AIBinder *binder, transaction_code_t code,
                                  const AParcel *input, AParcel *output) {
    (void)binder; (void)code; (void)input; (void)output;
    return STATUS_UNKNOWN_TRANSACTION;
}

static void *run(void *unused) {
    (void)unused;
    // Some ROMs do not export this platform accessor.
    AIBinder *(*check)(const char *) = (AIBinder *(*)(const char *))dlsym(RTLD_DEFAULT, "AServiceManager_checkService");
    AIBinder *service = check ? check("SurfaceFlinger") : NULL;
    AIBinder_Class *type = AIBinder_Class_define("android.ui.ISurfaceComposer", on_create, on_destroy, on_transact);
    if (!service || !type || !AIBinder_associateClass(service, type)) {
        if (service) AIBinder_decStrong(service);
        MB_LOG(mb_log_enabled(), ANDROID_LOG_WARN, "Cleanup repaint unavailable; blur disarmed");
        return NULL;
    }
    atomic_store(&ready, true);
    for (;;) {
        pthread_mutex_lock(&worker_mutex);
        while (!pending) pthread_cond_wait(&wake, &worker_mutex);
        uint64_t session = pending;
        pending = 0;
        pthread_mutex_unlock(&worker_mutex);
        if (mb_lease() != session) continue;
        AParcel *input = NULL, *output = NULL;
        binder_status_t status = AIBinder_prepareTransaction(service, &input);
        if (status == STATUS_OK) {
            // 1004 requests composition without changing HWC/debug state.
            status = AIBinder_transact(service, 1004, &input, &output, FLAG_ONEWAY);
        }
        if (input) AParcel_delete(input);
        if (output) AParcel_delete(output);
        if (status != STATUS_OK) {
            atomic_store(&ready, false);
            MB_LOG(mb_log_enabled(), ANDROID_LOG_WARN, "Cleanup repaint failed: %d; blur disarmed", status);
            AIBinder_decStrong(service);
            return NULL;
        }
    }
}

static void start_worker(void) {
    // Keep Binder work off the RenderEngine thread; the worker lives until exit.
    pthread_t thread;
    if (!pthread_create(&thread, NULL, run, NULL)) pthread_detach(thread);
}

bool mb_repaint_available(void) {
    pthread_once(&worker_once, start_worker);
    return atomic_load(&ready);
}

void mb_request_repaint(uint64_t session) {
    if (!mb_repaint_available() || !session) return;
    pthread_mutex_lock(&worker_mutex);
    // Coalesce requests; only the latest session needs a cleanup frame.
    pending = session;
    pthread_cond_signal(&wake);
    pthread_mutex_unlock(&worker_mutex);
}
