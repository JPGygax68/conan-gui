#include <iostream>
#include <cstdio>
#include "./database.h"
#include "./repo_reader.h"


int main(int argc, char *argv[])
{
    Conan::Database database;
    Conan::Repository_reader repo_reader{database};

    auto future = repo_reader.requeue_all();

    std::cin.ignore();
}