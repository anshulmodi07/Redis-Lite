#include <iostream>
#include <thread>

using namespace std;

void worker()
{
    cout << "Worker Running\n";
}

int main()
{
    thread t(worker);

    t.detach();

    cout << "Main Running\n";
}