#include "bh_ui_dev_console_view.h"

#include "../bh_interface.h"
#include "../bh_rml_event_binding.h"
#include "../bh_ui_registry.h"

#include "debug/bh_console.h"
#include "debug/bh_dev_console.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/Traits.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace
{

static std::string bh_trim(std::string s)
{
    auto not_space = [](unsigned char c) { return !std::isspace(c); };

    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

static std::vector<std::string> bh_split_words(const std::string &s)
{
    std::vector<std::string> out;
    std::string cur;
    bool in_word = false;

    for (char ch : s)
    {
        if (std::isspace((unsigned char)ch))
        {
            if (in_word)
            {
                out.push_back(cur);
                cur.clear();
                in_word = false;
            }
        }
        else
        {
            cur.push_back(ch);
            in_word = true;
        }
    }
    if (in_word)
        out.push_back(cur);

    return out;
}

class BH_UIDevConsoleView final : public BH_UIView
{
  public:
    static constexpr std::string_view kTypeName = "dev_console";
    static constexpr std::string_view kRmlPath = "interface/dev_console.rml";

    std::string_view TypeName() const override
    {
        return kTypeName;
    }
    std::string_view RmlDocumentPath() const override
    {
        return kRmlPath;
    }

    BH_UIViewFlags Flags() const override
    {
        return BH_UIViewFlags::BlockGameInput | BH_UIViewFlags::CaptureMouse | BH_UIViewFlags::CaptureKeyboard;
    }

  protected:
    bool OnLoad(BH_Interface *iface) override
    {
        (void)iface;

        Rml::ElementDocument *doc = Document();
        if (!doc)
        {
            SDL_Log("[bh][ui] dev_console: missing document");
            return false;
        }

        log_el_ = doc->GetElementById("console_log");
        if (!log_el_)
        {
            SDL_Log("[bh][ui] dev_console: missing element #console_log");
            return false;
        }

        Rml::Element *input_el = doc->GetElementById("console_input");
        if (!input_el)
        {
            SDL_Log("[bh][ui] dev_console: missing element #console_input");
            return false;
        }

        input_ = rmlui_dynamic_cast<Rml::ElementFormControlInput *>(input_el);
        if (!input_)
        {
            SDL_Log("[bh][ui] dev_console: #console_input is not an <input>");
            return false;
        }

        // Handle command submit + history.
        keydown_.Bind(doc, "console_input", "keydown", [this, iface](Rml::Event &ev) {
            if (!this || !iface || !input_)
                return;

            const int key = ev.GetParameter<int>("key_identifier", 0);

            if (key == (int)Rml::Input::KI_RETURN)
            {
                ev.StopPropagation();
                SubmitCommand(iface);
            }
            else if (key == (int)Rml::Input::KI_UP)
            {
                ev.StopPropagation();
                HistoryPrev();
            }
            else if (key == (int)Rml::Input::KI_DOWN)
            {
                ev.StopPropagation();
                HistoryNext();
            }
        });

        // Initial banner.
        BH_DevConsole_Print("WR Developer Console ready.");
        BH_DevConsole_Print("Type 'help' for commands.");

        return true;
    }

    void OnShow(BH_Interface * /*iface*/) override
    {
        // Focus the input so typing works immediately when opened.
        if (input_)
        {
            input_->Focus();
            input_->SetValue("");
        }

        // Keep the log scrolled to the bottom when opened.
        ScrollLogToBottom();
    }

    void OnHide(BH_Interface * /*iface*/) override
    {
        // Nothing yet; keep history.
    }

    void OnUnload(BH_Interface * /*iface*/) override
    {
        keydown_.Unbind();
        log_el_ = nullptr;
        input_ = nullptr;
    }

    void OnUpdate(BH_Interface *iface, float /*dt*/) override
    {
        // Pull any pending lines from the global queue.
        BH_DevConsole_Flush(&BH_UIDevConsoleView::FlushThunk, this);

        // Keep the caret active.
        (void)iface;
    }

  private:
    static void FlushThunk(void *user, const char *line)
    {
        BH_UIDevConsoleView *self = reinterpret_cast<BH_UIDevConsoleView *>(user);
        if (self)
            self->AppendLine(line, /*as_cmd=*/false, /*as_err=*/false);
    }

    void AppendLine(const char *text, bool as_cmd, bool as_err)
    {
        if (!text || !text[0] || !log_el_)
            return;

        Rml::ElementDocument *doc = Document();
        if (!doc)
            return;

        Rml::ElementPtr line_el = doc->CreateElement("p");
        if (!line_el)
            return;

        line_el->SetClassNames("console_line");
        if (as_cmd)
        {
            line_el->SetClassNames("console_line console_line_cmd");
        }
        else if (as_err)
        {
            line_el->SetClassNames("console_line console_line_err");
        }

        // Prefer a text node to avoid any markup interpretation.
        line_el->AppendChild(doc->CreateTextNode(text));
        log_el_->AppendChild(std::move(line_el));

        // Cap the number of rendered lines to avoid unbounded growth.
        constexpr int kMaxLines = 300;
        while (log_el_->GetNumChildren() > kMaxLines)
        {
            if (Rml::Element *first = log_el_->GetChild(0))
            {
                (void)log_el_->RemoveChild(first);
            }
            else
            {
                break;
            }
        }

        ScrollLogToBottom();
    }

    void ScrollLogToBottom()
    {
        if (!log_el_)
            return;

        // ScrollHeight can be 0 before the next layout pass; this still helps.
        log_el_->SetScrollTop(log_el_->GetScrollHeight());
    }

    void ClearRenderedLog()
    {
        if (!log_el_)
            return;

        while (log_el_->GetNumChildren() > 0)
        {
            if (Rml::Element *first = log_el_->GetChild(0))
            {
                (void)log_el_->RemoveChild(first);
            }
            else
            {
                break;
            }
        }
    }

    void SubmitCommand(BH_Interface *iface)
    {
        if (!input_)
            return;

        std::string cmd = input_->GetValue().c_str();
        input_->SetValue("");
        input_->Focus();

        cmd = bh_trim(cmd);
        if (cmd.empty())
            return;

        // Echo the command.
        {
            std::string echo = "] " + cmd;
            AppendLine(echo.c_str(), /*as_cmd=*/true, /*as_err=*/false);
        }

        // History.
        if (history_.empty() || history_.back() != cmd)
        {
            history_.push_back(cmd);
        }
        history_index_ = (int)history_.size();

        RunCommand(iface, cmd);
    }

    void HistoryPrev()
    {
        if (!input_ || history_.empty())
            return;

        history_index_ = std::max(0, history_index_ - 1);
        input_->SetValue(history_[(size_t)history_index_].c_str());
        input_->Focus();
        input_->Select();
    }

    void HistoryNext()
    {
        if (!input_)
            return;

        if (history_.empty())
        {
            input_->SetValue("");
            return;
        }

        history_index_ = std::min((int)history_.size(), history_index_ + 1);
        if (history_index_ >= (int)history_.size())
        {
            input_->SetValue("");
        }
        else
        {
            input_->SetValue(history_[(size_t)history_index_].c_str());
            input_->Select();
        }
        input_->Focus();
    }

    void RunCommand(BH_Interface *iface, const std::string &cmdline)
    {
        const std::vector<std::string> words = bh_split_words(cmdline);
        if (words.empty())
            return;

        const std::string &cmd = words[0];

        // View-local command.
        if (cmd == "clear")
        {
            ClearRenderedLog();
            return;
        }

        // Everything else routes through the global console registry.
        void *ctx = BH_Interface_GetUser(iface);
        const bool handled = BH_Console_Execute(ctx, cmdline.c_str());
        if (!handled)
        {
            AppendLine("Unknown command. Type 'help'.", false, true);
            return;
        }

        // Flush immediately so command results appear on the same frame.
        BH_DevConsole_Flush(&BH_UIDevConsoleView::FlushThunk, this);
    }

  private:
    Rml::Element *log_el_ = nullptr;
    Rml::ElementFormControlInput *input_ = nullptr;

    BH_Rml::EventBinding keydown_;

    std::vector<std::string> history_;
    int history_index_ = 0;
};

BH_REGISTER_VIEW(BH_UIDevConsoleView, "dev_console");

} // namespace
