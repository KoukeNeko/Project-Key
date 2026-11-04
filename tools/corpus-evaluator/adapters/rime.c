// Engine adapter: reads one Daqian key sequence per line on stdin, writes
// "<per-key ms, comma separated>\t<converted text>" per line on stdout.
// Usage: rime <rime-dir-with-shared-and-user> <schema_id>
#include <rime_api.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static double now_ms(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

int main(int argc, char **argv) {
  if (argc != 3) { fprintf(stderr, "usage: %s <rime-dir> <schema_id>\n", argv[0]); return 2; }
  char shared[1024], user[1024];
  snprintf(shared, sizeof shared, "%s/shared", argv[1]);
  snprintf(user, sizeof user, "%s/user", argv[1]);

  RimeApi *api = rime_get_api();
  RIME_STRUCT(RimeTraits, traits);
  traits.shared_data_dir = shared;
  traits.user_data_dir = user;
  traits.app_name = "rime.projectkey.evaluator";
  traits.min_log_level = 3;
  api->setup(&traits);
  api->initialize(NULL);
  RimeSessionId session = api->create_session();
  if (!api->select_schema(session, argv[2])) { fprintf(stderr, "cannot select schema %s\n", argv[2]); return 1; }

  char line[4096];
  while (fgets(line, sizeof line, stdin)) {
    line[strcspn(line, "\n")] = 0;
    char text[4096] = "";
    for (char *k = line; *k; k++) {
      double start = now_ms();
      api->process_key(session, (unsigned char)*k, 0);
      RIME_STRUCT(RimeContext, ctx);
      api->get_context(session, &ctx);  // a frontend redraws preedit and menu after every key
      fprintf(stdout, "%s%.3f", k == line ? "" : ",", now_ms() - start);
      if (ctx.commit_text_preview) snprintf(text, sizeof text, "%s", ctx.commit_text_preview);
      api->free_context(&ctx);
    }
    // Composition is cleared, never committed, so the user dictionary does not learn between sentences.
    api->clear_composition(session);
    fprintf(stdout, "\t%s\n", text);
    fflush(stdout);
  }
  api->destroy_session(session);
  api->finalize();
  return 0;
}
