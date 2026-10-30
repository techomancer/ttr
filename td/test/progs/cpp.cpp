// cpp.cpp - C++ for the debugger: functions in namespaces and classes,
// overloads, a template, a constructor, a virtual function and an
// operator, under their names as written.
namespace geo {

struct Shape {
    virtual ~Shape() {}
    virtual int area() const = 0;
};

struct Rect : Shape {
    int w, h;
    Rect(int w_, int h_) : w(w_), h(h_) {}
    int area() const { return w * h; }
};

int scale(int v)
{
    return v * 2;
}

int scale(int v, int by)
{
    return v * by;
}

} // namespace geo

template <class T>
T twice(T v)
{
    return v + v;
}

struct Counter {
    int n;
    Counter() : n(0) {}
    Counter& operator+=(int k)
    {
        n += k;
        return *this;
    }
};

int main()
{
    geo::Rect r(3, 4);
    geo::Shape* s = &r;
    Counter c;
    c += s->area();
    c += geo::scale(2);
    c += geo::scale(2, 3);
    c += twice(5);
    return c.n == 12 + 4 + 6 + 10 ? 0 : 1;
}
