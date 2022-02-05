#pragma once

#include <optional>
#include <string>

#include <toml++/toml.h>

#include <RE/T/TESDataHandler.h>

#include "FormId.hpp"
#include "FormError.hpp"

namespace RE {
    class TESDataHandler;
} // end namespace RE

template <typename T>
class Form {
protected:
    std::optional<FormId> formId_;
    T* form_ = nullptr;

public:
    static constexpr auto FormType = T::FORMTYPE;

    explicit Form() noexcept {}
    virtual ~Form() {}

    void setFromToml(const toml::array& arr);
    void loadForm(RE::TESDataHandler* dataHandler);
    void clear() noexcept
    {
        formId_.reset();
        form_ = nullptr;
    }

    const FormId& formId() const { return formId_.value(); }
    T* form() const noexcept { return form_; }

    bool isConfigLoaded() const noexcept { return formId_.has_value(); }
    bool isFormLoaded() const noexcept { return form_ != nullptr; }
};

template <typename T>
inline void Form<T>::setFromToml(const toml::array& arr)
{
    formId_.emplace(arr);
}

template <typename T>
inline void Form<T>::loadForm(RE::TESDataHandler* const dataHandler)
{
    using namespace std::literals;

    if (!formId_.has_value()) {
        return;
    }

    const auto& formId = formId_.value();
    auto form = dataHandler->LookupForm(formId.id(), formId.pluginName());

    if (form == nullptr) {
        throw MissingFormError(formId);
    }

    form_ = form->As<T>();

    if (form_ == nullptr) {
        throw UnexpectedFormTypeError(
            FormType,
            form->GetFormType(),
            form->GetName());
    }
}
