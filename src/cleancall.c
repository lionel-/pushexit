#define R_NO_REMAP
#include <Rinternals.h>

// Compats defined in init.c because the R API does not have a setter
// for external function pointers
SEXP cleancall_MakeExternalPtrFn(DL_FUNC p, SEXP tag, SEXP prot);
void cleancall_SetExternalPtrAddrFn(SEXP s, DL_FUNC p);


static SEXP callbacks = NULL;

// Preallocate a callback
static void push_callback(SEXP stack) {
  SEXP top = CDR(stack);

  SEXP fn_extptr = PROTECT(cleancall_MakeExternalPtrFn(NULL, R_NilValue, R_NilValue));
  SEXP data_extptr = PROTECT(R_MakeExternalPtr(NULL, R_NilValue, R_NilValue));
  SEXP cb = Rf_cons(Rf_cons(fn_extptr, data_extptr), top);

  SETCDR(stack, cb);

  UNPROTECT(2);
}


static void call_exits(void* data) {
  // Remove protecting node. Don't remove the preallocated callback on
  // the top as it might contain a handler when something went wrong.
  SEXP top = CDR(callbacks);

  // Restore old stack
  callbacks = (SEXP) data;

  // Handlers should not jump
  while (top != R_NilValue) {
    SEXP cb = CAR(top);
    top = CDR(top);

    void (*fn)(void*) = (void (*)(void*)) R_ExternalPtrAddrFn(CAR(cb));
    void *data = (void*) EXTPTR_PTR(CDR(cb));

    // Check for empty pointer in preallocated callbacks
    if (fn) {
      fn(data);
    }
  }
}

SEXP r_with_exit_context(SEXP (*fn)(void* data), void* data) {
  // Preallocate new stack before changing `callbacks` to avoid
  // leaving the global variable in a bad state if alloc fails
  SEXP new = PROTECT(Rf_cons(R_NilValue, R_NilValue));
  push_callback(new);

  SEXP old = callbacks;
  callbacks = new;

  SEXP out = R_ExecWithCleanup(fn, data, &call_exits, (void*) old);

  UNPROTECT(1);
  return out;
}

void r_push_exit(void (*fn)(void* data), void* data) {
  if (!callbacks) {
    fn(data);
    Rf_error("Internal error: Exit handler pushed outside of an exit context");
  }

  SEXP cb = CADR(callbacks);

  // Update pointers
  cleancall_SetExternalPtrAddrFn(CAR(cb), (DL_FUNC) fn);
  R_SetExternalPtrAddr(CDR(cb), data);

  // Preallocate the next callback in case the allocator jumps
  push_callback(callbacks);
}