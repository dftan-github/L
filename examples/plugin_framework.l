//看看L的插件框架干不干净
//我为了大家好看，把多模块放进一个文件中了
//实际按模块化分到4个文件，算上这个就是5个模块文件


// ============================================================================
// A minimal plugin framework written purely in L.
//
// Demonstrates interfaces, explicit implementation (no duck typing),
// Array<IPlugin> polymorphism, `this`-based instance access,
// for-in over a member field, and chained calls on interface values
// (`plugins.get(i).on_start()`).
//
// ----------------------------------------------------------------------------
// SINGLE FILE, LOGICALLY MULTI-MODULE
// ----------------------------------------------------------------------------
// The sections below are annotated as if each were its own module file. The
// code stays in THIS file (it compiles as one unit); the banners only record
// where each part belongs once the example is split into real modules.
// Each section already carries its `import` line, commented out right above the
// class/function it would guard — split the file and just uncomment them.
//
//   [MODULE] plugin_api.l      interface IPlugin          (no deps)
//   [MODULE] logger_plugin.l   class LoggerPlugin         imports plugin_api
//   [MODULE] auth_plugin.l     class AuthPlugin           imports plugin_api
//   [MODULE] app_host.l        class App                  imports plugin_api
//   [MODULE] plugin_framework.l  fun main                 imports all of the above
//
// Dependency graph (arrows point at what a module imports):
//
//   logger_plugin --> plugin_api <-- auth_plugin
//   app_host ------> plugin_api
//   plugin_framework --> plugin_api, logger_plugin, auth_plugin, app_host
//
// Note: a host that names `IPlugin` in field/parameter types must import
// plugin_api DIRECTLY, not only receive plugins transitively.
// ============================================================================


// ============================================================================
// [MODULE] plugin_api.l
//   The contract every plugin implements. No imports.
//   Declares: interface IPlugin
// ============================================================================
// BEGIN MODULE plugin_api

interface IPlugin {
    fun on_start()
    fun get_name() -> String
}

// END MODULE plugin_api


// ============================================================================
// [MODULE] logger_plugin.l
//   Concrete plugin #1.
//   Depends on: plugin_api  (import "plugin_api.l")
//   Declares: class LoggerPlugin
// ============================================================================
// BEGIN MODULE logger_plugin

// If this section is moved to logger_plugin.l, uncomment the line below.
// import "plugin_api.l"

class LoggerPlugin : IPlugin {
    fun init() {
    }

    fun on_start() {
        print(String("[start] logger ready"))
    }

    fun get_name() -> String {
        return String("logger")
    }
}

// END MODULE logger_plugin


// ============================================================================
// [MODULE] auth_plugin.l
//   Concrete plugin #2 — same contract, different behaviour.
//   Depends on: plugin_api  (import "plugin_api.l")
//   Declares: class AuthPlugin
// ============================================================================
// BEGIN MODULE auth_plugin

// If this section is moved to auth_plugin.l, uncomment the line below.
// import "plugin_api.l"

class AuthPlugin : IPlugin {
    fun init() {
    }

    fun on_start() {
        print(String("[start] auth ready"))
    }

    fun get_name() -> String {
        return String("auth")
    }
}

// END MODULE auth_plugin


// ============================================================================
// [MODULE] app_host.l
//   The registry/dispatcher. Knows only the IPlugin contract, never the
//   concrete plugins.
//   Depends on: plugin_api, properties (CPropertys), variant (Variant)
//   Declares: class App
// ============================================================================
// BEGIN MODULE app_host

// If this section is moved to app_host.l, uncomment the line below.
// import "plugin_api.l"

class App {
    Array<IPlugin> plugins
    delegate CPropertys

    fun init() {
        plugins = Array<IPlugin>()
        this.set("app_version", "0.1.0")
    }

    pub fun register(IPlugin p) {
        this.plugins.push(p)
    }

    pub fun start_all() {
        for p in this.plugins {
            p.on_start()
        }
    }

    // Chained dispatch straight off the array element — no temp variable.
    pub fun start_one(i32 i) {
        this.plugins.get(i).on_start()
    }

    pub fun name_at(i32 i) -> String {
        return this.plugins.get(i).get_name()
    }

    pub fun plugin_count() -> i32 {
        return this.plugins.len()
    }

    pub fun version() -> String {
        return this.get("app_version") ?? "0.1.0"
    }
}

// END MODULE app_host


// ============================================================================
// [MODULE] plugin_framework.l  (entry point)
//   Wiring only: build the App, register concrete plugins, drive dispatch.
//   Depends on: plugin_api, logger_plugin, auth_plugin, app_host
// ============================================================================
// BEGIN MODULE plugin_framework

// If this section is moved to plugin_framework.l (entry), uncomment the lines below.
// import "plugin_api.l"
// import "logger_plugin.l"
// import "auth_plugin.l"
// import "app_host.l"

fun main() -> i32 {
    app := App()

    print(String("app version: ") + app.version())

    app.register(LoggerPlugin())
    app.register(AuthPlugin())
    print(app.plugin_count())

    app.start_all()

    // Chained: element -> method call, both void and String-returning.
    app.start_one(0)
    print(app.name_at(1))

    for p in app.plugins {
        print(p.get_name())
    }
    return 0
}

// END MODULE plugin_framework

/*
输出
app version: 0.1.0
2
[start] logger ready
[start] auth ready
[start] logger ready
auth
logger
auth

//*/
