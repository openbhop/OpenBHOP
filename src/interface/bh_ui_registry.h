#pragma once

#include "bh_ui_view.h"
#include <functional>
#include <map>
#include <memory>
#include <string_view>

// Function signature for creating a view
using BH_ViewCreator = std::function<std::unique_ptr<BH_UIView>()>;

// Singleton registry accessor
inline std::map<std::string_view, BH_ViewCreator> &BH_GetViewRegistry()
{
    static std::map<std::string_view, BH_ViewCreator> reg;
    return reg;
}

// Helper struct that registers the view when declared statically
struct BH_ViewRegistrar
{
    BH_ViewRegistrar(std::string_view name, BH_ViewCreator creator)
    {
        BH_GetViewRegistry()[name] = creator;
    }
};

// The "Decorator" Macro
// Usage: BH_REGISTER_VIEW(MyViewClass, "view_name");
#define BH_REGISTER_VIEW(ClassType, StringName)                                                                        \
    static BH_ViewRegistrar _reg_##ClassType(                                                                          \
        StringName, []() -> std::unique_ptr<BH_UIView> { return std::make_unique<ClassType>(); })
