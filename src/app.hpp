#pragma once

#ifdef __EMSCRIPTEN__
#    include <emscripten.h>
#endif

struct Application {
    virtual ~Application() = default;

    virtual void init() = 0;
    virtual void loop() = 0;
    virtual void quit() = 0;

    virtual bool special_code_in_constructor() const noexcept { return false; }
    virtual bool special_code_in_deconstructor() const noexcept { return false; }

    static Application* get();

    bool running = true;

    protected:
    struct Exit {};
    struct ErrorExit : Exit {};
    friend int main();
};
