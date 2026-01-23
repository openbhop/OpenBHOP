#pragma once

#ifndef __cplusplus
#error "bh_rml_event_binding.h is C++-only."
#endif

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/EventListener.h>

#include <SDL3/SDL.h>

#include <functional>
#include <memory>
#include <string>

// Tiny helper to bind RmlUi events using inline std::function callbacks.
//
// Usage:
//   BH_RmlEventBinding bind;
//   bind.Bind(doc, "button_id", "click", [](Rml::Event& ev){ ... });
//
// The binding owns the listener and unbinds automatically on Unbind()/destruction.

namespace BH_Rml
{

class InlineEventListener final : public Rml::EventListener
{
  public:
    explicit InlineEventListener(std::function<void(Rml::Event &)> fn) : fn_(std::move(fn))
    {
    }

    void ProcessEvent(Rml::Event &event) override
    {
        if (fn_)
            fn_(event);
    }

  private:
    std::function<void(Rml::Event &)> fn_;
};

class EventBinding
{
  public:
    ~EventBinding()
    {
        Unbind();
    }

    EventBinding() = default;
    EventBinding(const EventBinding &) = delete;
    EventBinding &operator=(const EventBinding &) = delete;

    EventBinding(EventBinding &&other) noexcept
        : element_(other.element_), event_type_(std::move(other.event_type_)), listener_(std::move(other.listener_))
    {
        other.element_ = nullptr;
    }

    EventBinding &operator=(EventBinding &&other) noexcept
    {
        if (this != &other)
        {
            Unbind();
            element_ = other.element_;
            event_type_ = std::move(other.event_type_);
            listener_ = std::move(other.listener_);
            other.element_ = nullptr;
        }
        return *this;
    }

    void Bind(Rml::ElementDocument *doc, const char *element_id, const char *event_type,
              std::function<void(Rml::Event &)> fn)
    {
        Unbind();

        if (!doc || !element_id || !element_id[0] || !event_type || !event_type[0])
            return;

        Rml::Element *el = doc->GetElementById(element_id);
        if (!el)
        {
            SDL_Log("[bh][ui] Bind failed: element id '%s' not found", element_id);
            return;
        }

        element_ = el;
        event_type_ = event_type;
        listener_ = std::make_unique<InlineEventListener>(std::move(fn));
        element_->AddEventListener(event_type_, listener_.get());
    }

    void Unbind()
    {
        if (element_ && listener_ && !event_type_.empty())
        {
            element_->RemoveEventListener(event_type_, listener_.get());
        }

        listener_.reset();
        element_ = nullptr;
        event_type_.clear();
    }

    bool IsBound() const noexcept
    {
        return element_ != nullptr;
    }

  private:
    Rml::Element *element_ = nullptr;
    std::string event_type_;
    std::unique_ptr<Rml::EventListener> listener_;
};

} // namespace BH_Rml
