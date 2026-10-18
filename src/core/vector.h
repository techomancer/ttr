/*
 * Minimal growable array. T must be default constructible and assignable.
 * Not copyable; pass by reference.
 */
#ifndef TTR_CORE_VECTOR_H
#define TTR_CORE_VECTOR_H

namespace ttr {

template <class T>
class Vector {
public:
    Vector() : data_(0), size_(0), cap_(0) {}
    ~Vector() { delete [] data_; }

    int size() const { return size_; }
    bool empty() const { return size_ == 0; }
    T& operator[](int i) { return data_[i]; }
    const T& operator[](int i) const { return data_[i]; }
    T& back() { return data_[size_ - 1]; }

    void push_back(const T& v)
    {
        if (size_ == cap_)
            grow();
        data_[size_++] = v;
    }

    void insert_front(const T& v)
    {
        push_back(v);
        int j;
        for (j = size_ - 1; j > 0; j--)
            data_[j] = data_[j - 1];
        data_[0] = v;
    }

    void remove_at(int i)
    {
        int j;
        for (j = i; j + 1 < size_; j++)
            data_[j] = data_[j + 1];
        size_--;
    }

    /* Index of the first element equal to v, or -1. */
    int find(const T& v) const
    {
        int i;
        for (i = 0; i < size_; i++)
            if (data_[i] == v)
                return i;
        return -1;
    }

    void clear() { size_ = 0; }

private:
    Vector(const Vector&);
    Vector& operator=(const Vector&);

    void grow()
    {
        int ncap = cap_ ? cap_ * 2 : 8;
        T* n = new T[ncap];
        int i;
        for (i = 0; i < size_; i++)
            n[i] = data_[i];
        delete [] data_;
        data_ = n;
        cap_ = ncap;
    }

    T* data_;
    int size_;
    int cap_;
};

}

#endif
