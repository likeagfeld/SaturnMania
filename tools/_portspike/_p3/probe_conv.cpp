struct Foo { Foo(); int x; };
Foo::Foo() { x = 7; }
Foo g_foo;           // global with non-trivial ctor
int main(void) { return g_foo.x; }
