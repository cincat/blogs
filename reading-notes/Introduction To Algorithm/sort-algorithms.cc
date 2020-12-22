#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <limits>
#include <algorithm>

#include <cstdlib>
#include <sys/time.h>

void print_array(std::string prefix, const std::vector<int> &array) {
    std::cout << prefix << std::endl;
    for (auto x : array) {
        std::cout << x << " ";
    }
    std::cout << std::endl;
}

void timer_recoder(std::string label, std::function<void(std::vector<int>)> foo, std::vector<int> &array) {
    struct timeval bval, eval;
    gettimeofday(&bval, nullptr);
    foo(array);
    gettimeofday(&eval, nullptr);
    std::cout << label << " cost: ";
    std::cout << (eval.tv_sec + eval.tv_usec/1e6 - bval.tv_sec - bval.tv_usec/1e6);
    std::cout << "s" << std::endl;
}

void bubble_sort(std::vector<int> array) {
    for (int i = 0; i < array.size(); i++) {
        for (int j = array.size() - 1; j > i; j--) {
            if (array[j] < array[j - 1]) {
                std::swap(array[j - 1], array[j]);
            }
        }
    }
    print_array("bubble sort result", array);
}

void insert_sort(std::vector<int> array) {
    for (int i = 1; i < array.size(); i++) {
        int key = array[i], j = i - 1;
        for (; j >= 0; j--) {
            if (key >= array[j]) {break;}
            array[j + 1] = array[j];
        }
        array[j + 1] = key;
    }
    print_array("insert sort result", array);
}

void select_sort(std::vector<int> array) {
    for (int i = 0; i < array.size(); i++) {
        int minval = std::numeric_limits<int>::max();
        int minind = 0;
        for (int j = i; j < array.size(); j++) {
            if (array[j] < minval) {
                minval = array[j];
                minind = j; 
            }
        }
        std::swap(array[i], array[minind]);
    }
    print_array("select sort result", array);
}

void merge(std::vector<int> &array, int start, int end, int mid) {

    // pay attention: 构造函数是左开右闭区间
    std::vector<int> left(array.begin() + start, array.begin() + mid + 1);
    std::vector<int> right(array.begin() + mid + 1, array.begin() + end + 1);

    left.push_back(std::numeric_limits<int>::max());
    right.push_back(std::numeric_limits<int>::max());

    int i = 0, j = 0;
    for (int k = start; k <= end; k++) {
        if (left[i] <= right[j]) {
            array[k] = left[i++]; 
        }
        else {
            array[k] = right[j++];
        }
    }
}

void merge_sort_internal(std::vector<int> &array, int start, int end) {
    if(start < end) {
        int mid = (start + end)/2;
        merge_sort_internal(array, start, mid);
        merge_sort_internal(array, mid + 1, end);
        merge(array, start, end, mid);
    }
}

void heapify(std::vector<int> &array, int root, int heap_size) {
    int next_root = root;
    int l = 2 * root, r = 2 * root + 1;
    if (l < heap_size && array[root] < array[l]) next_root = l;
    if (r < heap_size && array[next_root] < array[r]) next_root = r;

    if (next_root != root) {
        std::swap(array[next_root], array[root]);
        heapify(array, next_root, heap_size);
    }
}

void heap_sort(std::vector<int> array) {
    int heap_size = array.size();
    for (int i = array.size() / 2; i >= 0; i--) {
        heapify(array, i, heap_size);
    }

    
    for (int i = array.size() - 1; i > 0; i--) {
        std::swap(array[0], array[heap_size - 1]);
        heapify(array, 0, --heap_size);
    }
    print_array("heap_sort", array);
}

void merge_sort(std::vector<int> array) {
    merge_sort_internal(array, 0, array.size() - 1);
    print_array("merge sort result", array);
}

int partition(std::vector<int> &array, int start, int end) {
    int i = start - 1, key = array[end - 1];
    for (int j = start; j < end - 1; j++) {
        if (array[j] < key) {
            std::swap(array[j], array[++i]);
        }
    }
    std::swap(array[++i], array[end - 1]);
    return i;
}

void quick_sort_internal(std::vector<int> &array, int start, int end) {
    
    if (start < end) {
        int mid = partition(array, start, end);
        quick_sort_internal(array, start, mid);
        quick_sort_internal(array, mid + 1, end);
    }
   
}

void quick_sort(std::vector<int> array) {
    quick_sort_internal(array, 0, array.size());
    print_array("quick sort", array);
}

int main() {
    std::cout << "please enter test data size(for example: 100)" << std::endl;
    int n;
    std::cin >> n;
    std::vector<int> array(n, 0);
    for (int i = 0; i < n; i++) {
        array[i] = rand() % n;
    }

    print_array("original array", array);

    timer_recoder("bububle sort", bubble_sort, array);
    timer_recoder("select sort", select_sort, array);
    timer_recoder("insert_sort", insert_sort, array);
    timer_recoder("merge_sort", merge_sort, array);
    timer_recoder("quick_sort", quick_sort, array);
    return 0;
}