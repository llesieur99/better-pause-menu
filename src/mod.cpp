#include <string>
#include "openrac/mod.hpp"
#include "logic.hpp"

void load(openrac::Mod& mod){
    if(mod.game()!="rac1" || !mod.has_pause_items() || !mod.has_play()) throw openrac::Error("Better Pause Menu requires RAC1 interface 1.9+");
    const auto page=mod.ui_page([&mod](const openrac::UiPage& page,const openrac::UiEvent& event){
        auto status=mod.play_status();
        openrac_pose pose{}; pose.size=sizeof pose;
        const bool hero=mod.hero_pose(pose);
        const bool scene=mod.has_scenes() && mod.scene_playing()>=0;
        openrac::UiContent c;
        c.title="QoL Hub";
        c.items={
            openrac::UiItem::text(status.level>=0 ? "Planet: "+mod.level_name(status.level) : "Planet: -"),
            openrac::UiItem::text(qol::hp_line(hero?pose.health:0, mod.has_world()?mod.bolts():0)),
            openrac::UiItem::button("Restart current planet",status.level>=0),
            openrac::UiItem::button("Skip current cutscene",scene)
        };
        page.set(c);
        if(event.is(OPENRAC_UI_CHOOSE) && event.item()==2 && status.level>=0) mod.play_travel(status.level);
        if(event.is(OPENRAC_UI_CHOOSE) && event.item()==3 && scene) mod.play_skip();
    });
    mod.ui_pause_item("QoL Hub",page);
    mod.info("Pause > Options > QoL Hub");
}
OPENRAC_MOD_CPP("better-pause-menu","0.1.0",load)
