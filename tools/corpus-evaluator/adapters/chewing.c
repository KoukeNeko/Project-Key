// Engine adapter: reads one Daqian key sequence per line on stdin, writes
// "<per-key ms, comma separated>\t<converted text>" per line on stdout.
// Usage: chewing <system-dict-dir> <user-dict-file>
#include <chewing/chewing.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static double now_ms(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

int main(int argc, char **argv) {
  if (argc != 3) { fprintf(stderr, "usage: %s <system-dict-dir> <user-dict-file>\n", argv[0]); return 2; }
  ChewingContext *ctx = chewing_new2(argv[1], argv[2], NULL, NULL);
  if (!ctx) { fprintf(stderr, "chewing_new2 failed\n"); return 1; }
  chewing_set_KBType(ctx, KB_DEFAULT);  // Daqian, same keys as the RIME adapter
  chewing_set_maxChiSymbolLen(ctx, MAX_CHI_SYMBOL_LEN);
  chewing_set_autoLearn(ctx, AUTOLEARN_DISABLED);

  char line[4096];
  while (fgets(line, sizeof line, stdin)) {
    line[strcspn(line, "\n")] = 0;
    for (char *k = line; *k; k++) {
      double start = now_ms();
      if (*k == ' ') chewing_handle_Space(ctx); else chewing_handle_Default(ctx, (unsigned char)*k);
      const char *preedit = chewing_buffer_String_static(ctx);  // a frontend redraws the preedit after every key
      (void)preedit;
      fprintf(stdout, "%s%.3f", k == line ? "" : ",", now_ms() - start);
    }
    fprintf(stdout, "\t%s\n", chewing_buffer_String_static(ctx));
    fflush(stdout);
    chewing_clean_preedit_buf(ctx);
  }
  chewing_delete(ctx);
  return 0;
}
