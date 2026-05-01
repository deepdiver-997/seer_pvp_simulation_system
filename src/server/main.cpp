#include <iostream>
#include <exception>

#include <entities/pet_factory.h>
#include <server.h>

int main() {
    if (!PetFactory::initialize_runtime_data()) {
        std::cerr << "failed to initialize runtime data" << std::endl;
        return 1;
    }

    try {
        Server server;
        server.start();
    } catch (const std::exception& ex) {
        std::cerr << "server crashed: " << ex.what() << std::endl;
        return 1;
    }

    return 0;
}
