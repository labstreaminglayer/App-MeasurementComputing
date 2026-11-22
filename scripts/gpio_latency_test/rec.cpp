// rec OUT.bin SECONDS [chan=1] [stream=MCCDaq]: dump (double lsl_ts, int16 value) of one channel of an int16 stream.
#include <lsl_cpp.h>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
int main(int argc, char **argv) {
	double dur = atof(argv[2]);
	int ch = argc > 3 ? atoi(argv[3]) : 1;
	auto found = lsl::resolve_stream("name", argc > 4 ? argv[4] : "MCCDaq", 1, 10.0);
	if (found.empty()) { puts("stream not found"); return 1; }
	lsl::stream_inlet in(found[0], 360, 0, false);  // no postprocessing: raw outlet stamps
	int nch = in.info().channel_count();
	in.open_stream(5.0);
	FILE *f = fopen(argv[1], "wb");
	std::vector<int16_t> buf;
	std::vector<double> ts;
	long n = 0;
	double t0 = lsl::local_clock();
	while (lsl::local_clock() - t0 < dur) {
		if (!in.pull_chunk_multiplexed(buf, &ts, 0.0)) { std::this_thread::sleep_for(std::chrono::milliseconds(2)); continue; }
		for (size_t k = 0; k < ts.size(); k++) { fwrite(&ts[k], 8, 1, f); fwrite(&buf[k * nch + ch], 2, 1, f); }
		n += ts.size();
	}
	fclose(f);
	printf("%ld samples\n", n);
}
