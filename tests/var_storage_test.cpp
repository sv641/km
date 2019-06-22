
/*
 * Copyright © 2018 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 *
 * Simple check for different storage classes and related linkage for KM payload
 */

#include <chrono>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

using namespace std;

#include <stdio.h>
#include <string.h>

class StorageType
{
   int var;
   pthread_t me;

   void check_id(void)
   {
      if (me != pthread_self()) {
         cout << "BAD BAD THREAD STORAGE: me=" << std::hex << me << " self=" << std::hex
              << pthread_self() << endl;
      }
   }

 public:
   string name;

   StorageType(const char* _n)
   {
      me = pthread_self();
      name = _n;
      pname("Constructor");
   }

   ~StorageType(void) { pname("Destructor"); }

   // void pname(void) { cout << "print name: " + name << endl; }
   void pname(string tag)
   {
      check_id();
      ostringstream id;
      id << "me=0x" << hex << me << " " << tag << " " << name << " thr=0x" << hex << pthread_self()
         << endl;
      cout << id.str();
   }
};

static StorageType t_GlobalStatic("Global static");
StorageType t_Global("Global visible");
static thread_local StorageType t_Tls("tls");

// for thread with entry function
void thread_entry(string n)
{
   string s = "thr " + n + " tls.name=" + t_Tls.name;
   t_Tls.pname(s.c_str());
   // this_thread::sleep_for(chrono::seconds(1));
}

// For thread with callable entry
class check_tls
{
 public:
   void operator()(string n)
   {
      string s = "Thread.operator() tls.name=" + t_Tls.name;
      cout << s << endl;
      t_Tls.pname(s.c_str());
   }
};

int main()
{
   StorageType t_main("Local main");
   static StorageType t_LocalStatic("local Static");

   thread t1(thread_entry, "1");
   thread t2(thread_entry, "2");
   thread t3(check_tls(), "3+");

   t_main.pname("t_main");
   t_LocalStatic.pname("t_LocalStatic");
   t_Global.pname("t_Global");
   t_GlobalStatic.pname("t_GlobalStatic");

   t1.join();
   t3.join();
   t2.join();

   cout << "after join" << endl;
   cout.flush();

   return 0;
}