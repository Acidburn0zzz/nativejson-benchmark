#include <algorithm>
#include <cfloat>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include "test.h"
#include "timer.h"
#include "resultfilename.h"

static const unsigned cTrialCount = 10;

struct TestJson {
    char* filename;
    char* json;
    size_t length;
};

typedef std::vector<TestJson> TestJsonList;
static TestJsonList gTestJsons;

static bool ReadFiles(const char* path) {
    char fullpath[FILENAME_MAX];
    sprintf(fullpath, path, "data.txt");
    FILE* fp = fopen(fullpath, "r");
    if (!fp)
        return false;

    while (!feof(fp)) {
        char filename[FILENAME_MAX];
        if (fscanf(fp, "%s", filename) == 1) {
            sprintf(fullpath, path, filename);
            FILE *fp2 = fopen(fullpath, "rb");
            if (!fp2) {
                printf("Cannot read '%s'\n", filename);
                continue;
            }
            
            TestJson t;
            t.filename = strdup(filename);
            fseek(fp2, 0, SEEK_END);
            t.length = (size_t)ftell(fp2);
            fseek(fp2, 0, SEEK_SET);
            t.json = (char*)malloc(t.length + 1);
            fread(t.json, 1, t.length, fp2);
            t.json[t.length] = '\0';
            fclose(fp2);

            printf("Read '%s' (%u bytes)\n", t.filename, (unsigned)t.length);

            gTestJsons.push_back(t);
        }
    }

    fclose(fp);
    printf("\n");
    return true;
}

static void FreeFiles() {
    for (TestJsonList::iterator itr = gTestJsons.begin(); itr != gTestJsons.end(); ++itr) {
        free(itr->filename);
        free(itr->json);
        itr->filename = 0;
        itr->json = 0;
    }
}

static void PrintStat(const Stat& stat) {
    printf("objectCount:  %10u\n", (unsigned)stat.objectCount);
    printf("arrayCount:   %10u\n", (unsigned)stat.arrayCount);
    printf("numberCount:  %10u\n", (unsigned)stat.numberCount);
    printf("stringCount:  %10u\n", (unsigned)stat.stringCount);
    printf("trueCount:    %10u\n", (unsigned)stat.trueCount);
    printf("falseCount:   %10u\n", (unsigned)stat.falseCount);
    printf("nullCount:    %10u\n", (unsigned)stat.nullCount);
    printf("memberCount:  %10u\n", (unsigned)stat.memberCount);
    printf("elementCount: %10u\n", (unsigned)stat.elementCount);
    printf("stringLength: %10u\n", (unsigned)stat.stringLength);
}

static void Verify(const TestBase& test) {
    printf("Verifying %s ... ", test.GetName());
    bool failed = false;

    for (TestJsonList::iterator itr = gTestJsons.begin(); itr != gTestJsons.end(); ++itr) {
        void* dom1 = test.Parse(itr->json);
        if (!dom1) {
            printf("\nFailed to parse '%s'\n", itr->filename);
            failed = true;
            continue;
        }

        Stat stat1 = test.Statistics(dom1);

        char* json1 = test.Stringify(dom1);
        test.Free(dom1);

        if (!json1) {
            printf("\nFailed to strinify '%s'\n", itr->filename);
            failed = true;
            continue;
        }

        void* dom2 = test.Parse(json1);
        if (!dom2) {
            printf("\nFailed to parse '%s' 2nd time\n", itr->filename);
            failed = true;
            continue;
        }

        Stat stat2 = test.Statistics(dom2);

        char* json2 = test.Stringify(dom2);
        test.Free(dom2);

        if (memcmp(&stat1, &stat2, sizeof(Stat)) != 0) {
            printf("\nFailed to rountrip '%s' (stats are different)\n", itr->filename);
            printf("1st time\n--------\n");
            PrintStat(stat1);
            printf("\n2nd time\n--------\n");
            PrintStat(stat2);
            printf("\n");

            // Write out json1 for diagnosis
            char filename[FILENAME_MAX];
            sprintf(filename, "%s_%s", test.GetName(), itr->filename);
            FILE* fp = fopen(filename, "wb");
            fwrite(json1, strlen(json1), 1, fp);
            fclose(fp);

            failed = true;
        }

        free(json1);
        free(json2);
    }

    printf(failed ? "Failed\n" : "OK\n");
}

static void VerifyAll() {
    TestList& tests = TestManager::Instance().GetTests();
    for (TestList::iterator itr = tests.begin(); itr != tests.end(); ++itr)
        Verify(**itr);    

    printf("\n");
}

static void BenchParse(const TestBase& test, FILE *fp) {
    for (TestJsonList::iterator itr = gTestJsons.begin(); itr != gTestJsons.end(); ++itr) {
        printf("Parse     %-20s ... ", itr->filename);
        fflush(stdout);

        double minDuration = DBL_MAX;
        for (unsigned trial = 0; trial < cTrialCount; trial++) {
            Timer timer;
            timer.Start();
            void* dom = test.Parse(itr->json);
            timer.Stop();

            test.Free(dom);
            double duration = timer.GetElapsedMilliseconds();
            minDuration = std::min(minDuration, duration);
        }
        double throughput = itr->length / (1024.0 * 1024.0) / (minDuration * 0.001);
        printf("%6.3f ms  %3.3f MB/s\n", minDuration, throughput);
        fprintf(fp, "Parse,%s,%s,%f\n", test.GetName(), itr->filename, minDuration);
    }
}

static void BenchStringify(const TestBase& test, FILE *fp) {
    for (TestJsonList::iterator itr = gTestJsons.begin(); itr != gTestJsons.end(); ++itr) {
        printf("Stringify %-20s ... ", itr->filename);
        fflush(stdout);

        double minDuration = DBL_MAX;
        void* dom = test.Parse(itr->json);

        for (unsigned trial = 0; trial < cTrialCount; trial++) {
            Timer timer;
            timer.Start();
            char* json = test.Stringify(dom);
            timer.Stop();

            free(json);
            double duration = timer.GetElapsedMilliseconds();
            minDuration = std::min(minDuration, duration);
        }

        test.Free(dom);

        double throughput = itr->length / (1024.0 * 1024.0) / (minDuration * 0.001);
        printf("%6.3f ms  %3.3f MB/s\n", minDuration, throughput);
        fprintf(fp, "Stringify,%s,%s,%f\n", test.GetName(), itr->filename, minDuration);
    }
}

static void BenchPrettify(const TestBase& test, FILE *fp) {
    for (TestJsonList::iterator itr = gTestJsons.begin(); itr != gTestJsons.end(); ++itr) {
        printf("Prettify  %-20s ... ", itr->filename);
        fflush(stdout);

        double minDuration = DBL_MAX;
        void* dom = test.Parse(itr->json);

        for (unsigned trial = 0; trial < cTrialCount; trial++) {
            Timer timer;
            timer.Start();
            char* json = test.Prettify(dom);
            timer.Stop();

            free(json);
            double duration = timer.GetElapsedMilliseconds();
            minDuration = std::min(minDuration, duration);
        }

        test.Free(dom);

        double throughput = itr->length / (1024.0 * 1024.0) / (minDuration * 0.001);
        printf("%6.3f ms  %3.3f MB/s\n", minDuration, throughput);
        fprintf(fp, "Stringify,%s,%s,%f\n", test.GetName(), itr->filename, minDuration);
    }
}

static void BenchStatistics(const TestBase& test, FILE *fp) {
    for (TestJsonList::iterator itr = gTestJsons.begin(); itr != gTestJsons.end(); ++itr) {
        printf("Statistics  %-20s ... ", itr->filename);
        fflush(stdout);

        double minDuration = DBL_MAX;
        void* dom = test.Parse(itr->json);

        for (unsigned trial = 0; trial < cTrialCount; trial++) {
            Timer timer;
            timer.Start();
            test.Statistics(dom);
            timer.Stop();

            double duration = timer.GetElapsedMilliseconds();
            minDuration = std::min(minDuration, duration);
        }

        test.Free(dom);

        double throughput = itr->length / (1024.0 * 1024.0) / (minDuration * 0.001);
        printf("%6.3f ms  %3.3f MB/s\n", minDuration, throughput);
        fprintf(fp, "Statistics,%s,%s,%f\n", test.GetName(), itr->filename, minDuration);
    }
}

static void Bench(const TestBase& test, FILE *fp) {
    printf("Benchmarking %s\n", test.GetName());
    BenchParse(test, fp);
    BenchStringify(test, fp);
    BenchPrettify(test, fp);
    BenchStatistics(test, fp);
    printf("\n");
}

static void BenchAll() {
    // Try to write to /result path, where template.php exists
    FILE *fp;
    if ((fp = fopen("../../result/template.php", "r")) != NULL) {
        fclose(fp);
        fp = fopen("../../result/" RESULT_FILENAME, "w");
    }
    else if ((fp = fopen("../result/template.php", "r")) != NULL) {
        fclose(fp);
        fp = fopen("../result/" RESULT_FILENAME, "w");
    }
    else
        fp = fopen(RESULT_FILENAME, "w");

    fprintf(fp, "Type,Library,Filename,Time(ms)\n");

    TestList& tests = TestManager::Instance().GetTests();
    for (TestList::iterator itr = tests.begin(); itr != tests.end(); ++itr)
        Bench(**itr, fp);

    fclose(fp);

    printf("\n");
}

int main() {
    // Read files
    if (!ReadFiles("../data/%s"))
        ReadFiles("../../data/%s");

    // sort tests
    TestList& tests = TestManager::Instance().GetTests();
    std::sort(tests.begin(), tests.end());

    VerifyAll();
    BenchAll();

    FreeFiles();
}
