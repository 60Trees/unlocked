#pragma once

int main();

namespace Base {
    struct BaseClass {
        bool running = true;

        virtual ~BaseClass() = default;

        virtual void init() = 0;
        virtual void loop() = 0;
        virtual void quit() = 0;

        virtual bool is_null() { return false; }

        inline void check_running(BaseClass* oth) {
            if (!oth->running) running = false;
        }

        friend int ::main();

        static BaseClass&& get_null();
    };
}  // namespace Base
