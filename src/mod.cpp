#include <cstdint>
#include <string>
#include "openrac/mod.hpp"
#include "logic.hpp"

namespace {
openrac::Mod* g=nullptr;
std::uint16_t previous=0;
int action=0;

void summary(){
    const auto status=g->play_status();
    openrac_pose pose{}; pose.size=sizeof pose;
    const bool hero=g->hero_pose(pose);
    std::string text=status.level>=0 ? g->level_name(status.level) : std::string("No planet");
    text+=" | "+qol::hp_line(hero?pose.health:0,g->has_world()?g->bolts():0);
    if(g->has_notice()) g->play_notice(text);
}
}

void load(openrac::Mod& mod){
    if(mod.game()!="rac1" || !mod.has_play()) throw openrac::Error("Better Pause Menu requires RAC1 play API");
    g=&mod;

    if(mod.has_pause_items()){
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
        try {
            mod.ui_pause_item("QoL Hub",page);
            mod.info("Pause > Options > QoL Hub");
        } catch(const openrac::Error&) {
            mod.info("Pause slots full; using L1+R1+Up/Down/Select fallback");
        }
    }

    mod.on_pad([](int port,std::uint8_t* report,int size){
        if(port!=0 || report==nullptr || size<2) return;
        const auto now=qol::pad_buttons(report);
        const auto show=static_cast<std::uint16_t>(OPENRAC_PAD_L1|OPENRAC_PAD_R1|OPENRAC_PAD_UP);
        const auto restart=static_cast<std::uint16_t>(OPENRAC_PAD_L1|OPENRAC_PAD_R1|OPENRAC_PAD_DOWN);
        const auto skip=static_cast<std::uint16_t>(OPENRAC_PAD_L1|OPENRAC_PAD_R1|OPENRAC_PAD_SELECT);
        if(qol::combo_pressed(now,previous,show)) action=1;
        if(qol::combo_pressed(now,previous,restart)) action=2;
        if(qol::combo_pressed(now,previous,skip)) action=3;
        previous=now;
    });

    mod.on_frame([]{
        if(action==0) return;
        const int a=action; action=0;
        const auto status=g->play_status();
        if(a==1){ summary(); return; }
        if(a==2){
            if(status.level>=0 && status.phase==OPENRAC_PLAY_PLAYING) g->play_travel(status.level);
            else if(g->has_notice()) g->play_notice("Restart is available while playing");
            return;
        }
        if(a==3){
            if(g->has_scenes() && g->scene_playing()>=0) g->play_skip();
            else if(g->has_notice()) g->play_notice("No cutscene is playing");
        }
    });
}
OPENRAC_MOD_CPP("better-pause-menu","0.1.1",load)
