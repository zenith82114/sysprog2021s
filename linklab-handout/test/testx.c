#include <stdlib.h>

void main(void)
{
  void *a;

  a=malloc(1024);
  free(NULL);
  a=realloc(a,0);
  a=realloc(a,500);
  a=realloc(NULL,300);
  a=realloc(a,1000);
  a=realloc(a,300);

}
