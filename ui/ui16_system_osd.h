#pragma once
enum class Ui16SystemAction { None, Restart, ReturnToList };
struct Menu;
Menu *ui16_create_system_osd(Ui16SystemAction &action,bool can_restart);
