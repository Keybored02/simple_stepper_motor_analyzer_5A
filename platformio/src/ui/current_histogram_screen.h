#pragma once

#include "misc/elapsed.h"
#include "screen_manager.h"

class CurrentHistogramScreen : public screen_manager::Screen {
 public:
  CurrentHistogramScreen();
  virtual void setup(uint8_t screen_num) override;
  virtual void on_load() override;
  virtual void on_unload() override;
  virtual void loop() override;
  virtual void on_event(ui_events::UiEventId ui_event_id) override;

 private:
  Elapsed display_update_elapsed_;
  ui::Histogram histogram_;
  ui::Label average_current_label_;
};