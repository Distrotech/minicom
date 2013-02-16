#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <sys/time.h>
#include <sys/select.h>

int usleep(unsigned usecs)
{
	struct timeval t;

	t.tv_sec  = usecs/1000000;
	t.tv_usec = usecs%1000000;
	select(1, NULL, NULL, NULL, &t);
	return 0;
}

