#pragma once
class ContainerScreenController;
class ScreenView;
namespace lamium::inventory::game::transferTrace {
void start();
void stop();
void inspectView(ScreenView& view) noexcept;
void inspect(ContainerScreenController& controller, bool changed) noexcept;
}
