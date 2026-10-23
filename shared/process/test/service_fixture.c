#include <salts/thread.h>
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv) {
  FILE *file; int state = 1;
  if (argc < 4) return 2;
  if (!strcmp(argv[2],"slow")) { cmeta_sleep_ms(2000); return 0; }
  if (!strcmp(argv[2],"flood")) { unsigned i; for (i = 0; i < 4096; ++i) putchar('x'); return 0; }
  if (strcmp(argv[2],"Retro Camera Service")) return 3;
  if (!strcmp(argv[1],"query")) {
    file = fopen(argv[3],"r"); if (!file) return 4;
    if (fscanf(file,"%d",&state) != 1) { fclose(file); return 5; }
    fclose(file);
    printf("SERVICE_NAME: fixture\n STATE : %d localized text\n",state); return 0;
  }
  if (!strcmp(argv[1],"start")) {
    if (argc != 6 || strcmp(argv[4],"space value") || strcmp(argv[5],"quote\"\\tail\\")) return 6;
    state = 4;
  } else if (!strcmp(argv[1],"stop")) state = 1;
  else return 7;
  file = fopen(argv[3],"w"); if (!file) return 8;
  fprintf(file,"%d\n",state); if (fclose(file)) return 9;
  return 0;
}
