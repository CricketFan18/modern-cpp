#include<bits/stdc++.h>
using namespace std;

void AsyncFn()
{
    int x = 10;
     
    thread t([&]() {
        this_thread::sleep_for(chrono::seconds(1));
        cout << "inside thread - " << x << endl;
    });
    cout << "outside thread - " << x << endl;
    t.detach(); // wrong usage, crashes code, 
}

int main()
{
    AsyncFn();
    this_thread::sleep_for(chrono::seconds(2));
    return 0;
}