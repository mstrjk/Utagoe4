// CPU の並列実行。常駐 thread pool に範囲を分けて渡す。
// 各 index の結果は他の index に依存しない処理だけに使うので、thread 数が変わっても結果は同じになる。

#ifndef UTAGOE_PARALLEL_H
#define UTAGOE_PARALLEL_H

#include <functional>

namespace utagoe {

// [0, n) を最大 thread 数で分割し、fn(begin, end) を並列に呼ぶ。minChunk 未満の仕事は分割しない。
// 呼び出した thread も処理に加わり、全部終わってから戻る。
void parallelFor(long long n, long long minChunk, const std::function<void(long long, long long)>& fn);

// pool の thread 数 (呼び出し側を含む)。
int workerCount();

}

#endif
