// GPIO edge generator for timestamp calibration (Raspberry Pi 5). Toggles one line of /dev/gpiochipN (RP1 "pinctrl-rp1") at
// seeded random intervals; every write is bracketed by clock_gettime(CLOCK_MONOTONIC) (= liblsl's
// steady clock on Linux). The line is requested and configured as output through the GPIO v2
// character-device uAPI (linux/gpio.h, no libgpiod needed). With GPIO_EDGES_MMAP=1 (default) the
// writes then go straight to RP1's RIO SET/CLR registers via /dev/gpiomem0 and are followed by a
// read of RIO SYNC_IN: a PCIe read cannot pass the posted write, so t_after is taken after the pad
// input already shows the new level (checked on every edge). GPIO_EDGES_MMAP=0 uses the ioctl instead.
//   gpio_edges OUT.csv [seconds=1e9] [seed=1] [line=16] [chip=/dev/gpiochip0] [min_ms=150] [max_ms=350]
// CSV: edge,value,t_before_ns,t_after_ns. On exit (time up, SIGINT, SIGTERM) the line is driven low,
// then reconfigured as input and released.
// Build: gcc -O2 -Wall -o gpio_edges gpio_edges.c
#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stop_flag = 0;
static void on_sig(int s) { (void)s; stop_flag = 1; }
static int64_t ns(const struct timespec *t) { return (int64_t)t->tv_sec * 1000000000LL + t->tv_nsec; }

static int set_val(int lfd, int v) {
	struct gpio_v2_line_values lv = {.bits = (uint64_t)v, .mask = 1};
	return ioctl(lfd, GPIO_V2_LINE_SET_VALUES_IOCTL, &lv);
}

// RP1 GPIO block as mapped by /dev/gpiomem0: io_bank0 +0x00000, sys_rio0 +0x10000 (OUT +0, OE +4,
// SYNC_IN +8; atomic aliases XOR +0x1000, SET +0x2000, CLR +0x3000), pads_bank0 +0x20000
static volatile uint32_t *rio = NULL;
static unsigned g_line;
static int mm_set(int v) {
	rio[(v ? 0x2000 : 0x3000) / 4] = 1u << g_line;
	uint32_t in = rio[0x8 / 4]; // flushes the posted write and samples the pad
	return (int)((in >> g_line) & 1u) == v ? 0 : -1;
}

int main(int argc, char **argv) {
	if (argc < 2) { fprintf(stderr, "usage: %s OUT.csv [secs] [seed] [line] [chip] [min_ms] [max_ms]\n", argv[0]); return 2; }
	double secs = argc > 2 ? atof(argv[2]) : 1e9;
	unsigned seed = argc > 3 ? (unsigned)atoi(argv[3]) : 1;
	unsigned line = argc > 4 ? (unsigned)atoi(argv[4]) : 16;
	const char *chip = argc > 5 ? argv[5] : "/dev/gpiochip0";
	double min_ms = argc > 6 ? atof(argv[6]) : 150, max_ms = argc > 7 ? atof(argv[7]) : 350;
	FILE *f = fopen(argv[1], "w");
	if (!f) { perror("fopen"); return 1; }
	int cfd = open(chip, O_RDWR | O_CLOEXEC);
	if (cfd < 0) { perror(chip); return 1; }
	struct gpio_v2_line_request req;
	memset(&req, 0, sizeof req);
	req.offsets[0] = line;
	req.num_lines = 1;
	strcpy(req.consumer, "gpio_edges");
	req.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
	req.config.num_attrs = 1; // initial value low
	req.config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
	req.config.attrs[0].attr.values = 0;
	req.config.attrs[0].mask = 1;
	if (ioctl(cfd, GPIO_V2_GET_LINE_IOCTL, &req) < 0) { perror("GPIO_V2_GET_LINE_IOCTL"); return 1; }
	int lfd = req.fd;
	g_line = line;
	const char *mm = getenv("GPIO_EDGES_MMAP");
	int use_mm = !mm || atoi(mm);
	if (use_mm) {
		int mfd = open("/dev/gpiomem0", O_RDWR | O_SYNC | O_CLOEXEC);
		void *p = mfd < 0 ? MAP_FAILED : mmap(NULL, 0x30000, PROT_READ | PROT_WRITE, MAP_SHARED, mfd, 0);
		if (p == MAP_FAILED) { perror("gpiomem0"); use_mm = 0; }
		else {
			rio = (volatile uint32_t *)((char *)p + 0x10000);
			// sanity: ioctl drives the line, the RIO registers must agree (OE set, IN follows)
			int ok = (rio[1] >> line) & 1u;
			for (int k = 0; ok && k < 4; k++) {
				set_val(lfd, k & 1);
				usleep(1000);
				ok = (int)((rio[2] >> line) & 1u) == (k & 1);
			}
			for (int k = 0; ok && k < 4; k++) ok = mm_set(!(k & 1)) == 0;
			mm_set(0);
			if (!ok) { fprintf(stderr, "RIO sanity check failed, using ioctl\n"); use_mm = 0; }
		}
	}
	fprintf(stderr, "gpio_edges: mode %s\n", use_mm ? "mmap-rio" : "ioctl");
	signal(SIGINT, on_sig);
	signal(SIGTERM, on_sig);
	fprintf(f, "# gpio_edges chip=%s line=%u seed=%u min_ms=%g max_ms=%g mode=%s clock=CLOCK_MONOTONIC\n", chip, line, seed, min_ms, max_ms, use_mm ? "mmap-rio" : "ioctl");
	fprintf(f, "edge,value,t_before_ns,t_after_ns\n");
	srand(seed);
	struct timespec t_start, next, tb, ta;
	clock_gettime(CLOCK_MONOTONIC, &t_start);
	next = t_start;
	int v = 0;
	long edge = 0;
	while (!stop_flag) {
		double iv = min_ms + (max_ms - min_ms) * ((double)rand() / RAND_MAX);
		int64_t t = ns(&next) + (int64_t)(iv * 1e6);
		next.tv_sec = t / 1000000000LL;
		next.tv_nsec = t % 1000000000LL;
		if ((t - ns(&t_start)) / 1e9 > secs) break;
		while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL) == EINTR)
			if (stop_flag) break;
		if (stop_flag) break;
		v ^= 1;
		// wake the PCIe link to RP1 first: after ~100 ms idle the first access costs ~5 us (ASPM exit)
		if (use_mm) (void)rio[0x8 / 4];
		clock_gettime(CLOCK_MONOTONIC, &tb);
		int rc = use_mm ? mm_set(v) : set_val(lfd, v);
		clock_gettime(CLOCK_MONOTONIC, &ta);
		if (rc < 0) { fprintf(stderr, "write failed or pin did not follow (edge %ld)\n", edge); break; }
		fprintf(f, "%ld,%d,%lld,%lld\n", edge++, v, (long long)ns(&tb), (long long)ns(&ta));
		if (edge % 50 == 0) fflush(f);
	}
	if (use_mm) mm_set(0);
	set_val(lfd, 0);
	struct gpio_v2_line_config cfg;
	memset(&cfg, 0, sizeof cfg);
	cfg.flags = GPIO_V2_LINE_FLAG_INPUT;
	if (ioctl(lfd, GPIO_V2_LINE_SET_CONFIG_IOCTL, &cfg) < 0) perror("SET_CONFIG input");
	close(lfd);
	close(cfd);
	fprintf(f, "# end edges=%ld\n", edge);
	fclose(f);
	return 0;
}
