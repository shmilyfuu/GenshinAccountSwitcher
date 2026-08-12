#include <Windows.h>
#undef GetCurrentTime
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Xaml.Interop.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.XamlTypeInfo.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::XamlTypeInfo;
using namespace Microsoft::UI::Xaml::Markup;
using namespace Windows::UI::Xaml::Interop;

class PrototypeApp : public ApplicationT<PrototypeApp, IXamlMetadataProvider>
{
public:
    void OnLaunched(LaunchActivatedEventArgs const&)
    {
        Resources().MergedDictionaries().Append(XamlControlsResources());
        m_window = Window();
        m_window.Title(L"WinUI 3 C++ 启动基线");

        StackPanel panel;
        panel.HorizontalAlignment(HorizontalAlignment::Center);
        panel.VerticalAlignment(VerticalAlignment::Center);
        panel.Spacing(12);

        TextBlock title;
        title.Text(L"WinUI 3 原生 C++");
        title.FontSize(24);
        panel.Children().Append(title);

        Button button;
        button.Content(box_value(L"启动成功"));
        button.HorizontalAlignment(HorizontalAlignment::Center);
        panel.Children().Append(button);

        m_window.Content(panel);
        m_window.Activate();
    }

    IXamlType GetXamlType(TypeName const& type)
    {
        return m_provider.GetXamlType(type);
    }

    IXamlType GetXamlType(hstring const& fullname)
    {
        return m_provider.GetXamlType(fullname);
    }

    com_array<XmlnsDefinition> GetXmlnsDefinitions()
    {
        return m_provider.GetXmlnsDefinitions();
    }

private:
    Window m_window{ nullptr };
    XamlControlsXamlMetaDataProvider m_provider;
};

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    init_apartment();
    Application::Start([](auto&&) { make<PrototypeApp>(); });
    return 0;
}
