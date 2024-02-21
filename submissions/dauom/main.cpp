#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>  // For close()

#include <algorithm>
#include <cstring>
#include <future>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace std;

const char *INPUT_FILENAME = "input.txt";
const char *OUTPUT_FILENAME = "output.txt";
const int NUM_THREADS = thread::hardware_concurrency();

inline void handle_error(const char *msg) {
  perror(msg);
  exit(255);
}

struct MappedFile {
  const int fd;
  const size_t file_size;
  const char *file_data;
};

struct Result {
  double city_cost[111];
  double product_cost[111][111];
  unordered_map<string, int> product_id;
  unordered_map<string, int> city_id;
  Result() {
    fill_n(city_cost, 111, 0.0);
    fill_n((double *)product_cost, 111 * 111, 4e18);
  }
};

inline const MappedFile map_input() {
  int fd = open(INPUT_FILENAME, O_RDONLY);
  if (fd == -1) handle_error("map_input: open failed");

  struct stat sb;
  if (fstat(fd, &sb) == -1) handle_error("map_input: fstat failed");

  const char *addr = static_cast<const char *>(
      mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0u));
  if (addr == MAP_FAILED) handle_error("map_input: mmap failed");
  madvise((void *)addr, 0, MADV_SEQUENTIAL);

  return {fd, (size_t)sb.st_size, addr};
}

inline const string consume_str(char *&start) {
  string s;
  s.reserve(40);
  char c;
  while ((c = *start) != 0 && c != ',' && c != '\n') {
    s += c;
    start++;
  }
  start++;
  return s;
}

inline int find_or_create(unordered_map<string, int> &id_map, const string &k) {
  int id = -1;
  if (id_map.find(k) == id_map.end()) {
    id = id_map[k] = id_map.size();
  } else {
    id = id_map[k];
  }
  return id;
}

Result process_chunk(char *start, char *end) {
  if (*start != '\n') {
    start = (char *)rawmemchr(start, '\n');
  }
  start++;

  Result r;
  char *cur = start;
  while (cur < end) {
    string city = consume_str(cur);
    string product = consume_str(cur);
    string price = consume_str(cur);
    double dprice = stod(price);

    int cid = find_or_create(r.city_id, city);
    int pid = find_or_create(r.product_id, product);
    r.product_cost[cid][pid] = min(r.product_cost[cid][pid], dprice);
    r.city_cost[cid] += dprice;
  }

  return r;
}

vector<Result> process_concurrently(const MappedFile &mp) {
  char *start = (char *)mp.file_data;
  char *end = start + mp.file_size;
  const int block_size = mp.file_size / NUM_THREADS;

  vector<future<Result>> future_results;
  for (int i = 0; i < NUM_THREADS; i++) {
    future_results.emplace_back(async(process_chunk, start + i * block_size,
                                      min(start + (i + 1) * block_size, end)));
  }

  vector<Result> results;
  for (auto &fr : future_results) {
    results.emplace_back(fr.get());
  }
  return results;
}

Result merge(vector<Result> &results) {
  Result mr;
  for (auto &r : results) {
    for (auto &cid : r.city_id) {
      int ncid = find_or_create(mr.city_id, cid.first);
      mr.city_cost[ncid] += r.city_cost[cid.second];
      for (auto &pid : r.product_id) {
        int npid = find_or_create(mr.product_id, pid.first);
        mr.product_cost[ncid][npid] =
            min(mr.product_cost[ncid][npid],
                r.product_cost[cid.second][pid.second]);
      }
    }
  }
  return mr;
}

inline void ans(Result &result) {
  FILE *f = fopen(OUTPUT_FILENAME, "w");

  double min_city_cost = 4e18;
  string city = "";
  int city_id = -1;
  for (auto &cid : result.city_id) {
    double c = result.city_cost[cid.second];
    if (c < min_city_cost) {
      min_city_cost = c;
      city = cid.first;
      city_id = cid.second;
    }
  }
  fprintf(f, "%s %.2f\n", city.c_str(), min_city_cost);

  vector<pair<double, string>> products;
  for (auto &pid : result.product_id) {
    products.push_back({result.product_cost[city_id][pid.second], pid.first});
  }
  sort(products.begin(), products.end());
  products.resize(5);

  for (auto &p : products) {
    fprintf(f, "%s %.2f\n", p.second.c_str(), p.first);
  }
  fclose(f);
}

int main() {
  const MappedFile mp = map_input();

  vector<Result> results = process_concurrently(mp);
  Result result = merge(results);
  ans(result);

  // Unmap the memory and close the file
  munmap((void *)mp.file_data, mp.file_size);
  close(mp.fd);
  return 0;
}