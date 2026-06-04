#include "../include/memory_pool.h"

#include <iostream>
#include <string>

class User
{
public:
    User(int id,
         const std::string& name)
        : _id(id),
          _name(name)
    {
        std::cout
            << "User ctor\n";
    }

    ~User()
    {
        std::cout
            << "User dtor\n";
    }

    void print() const
    {
        std::cout
            << _id
            << " "
            << _name
            << '\n';
    }

private:
    int _id;
    std::string _name;
};

int main()
{
    MemoryPool pool(
        10,
        sizeof(User)
    );

    User* user =
        pool.newObject<User>(
            1001,
            "freedom"
        );

    user->print();

    pool.deleteObject(user);

    return 0;
}