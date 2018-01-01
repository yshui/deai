#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#define min(a,b) ((a)<(b)?(a):(b))

int main(int argc, const char **argv) {
	if (argc < 2)
		return 0;
	int credits = atoi(argv[1]),n;

startover:
	if (!credits) {
		//fprintf(stderr, "%d: no credit\n", getpid());
		goto end;
	}
	srand(getpid());
	n = rand()%min(3, credits)+1;
	credits -= n;
	//fprintf(stderr, "%d:%d %d\n", getpid(), n, credits);
	int childcredits = credits/n;
	for (int i = 0; i < n; i++) {
		//fprintf(stderr, " %d:%d %d\n", getpid(), i, childcredits+(i<credits%n));
		if (!fork()) {
			//child
			setpgid(0, 0);
			credits = childcredits + (i<credits%n);
			goto startover;
		}
	}
end:
	pause();
}
