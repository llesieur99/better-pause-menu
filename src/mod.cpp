#include <cstdint>
#include <string>
#include "openrac/mod.hpp"
#include "logic.hpp"

namespace {
openrac::Mod* g=nullptr;
openrac::UiPage hub;
std::uint16_t previous=0;
int action=0;
}

void load(openrac::Mod& mod){
    if(mod.game()!="rac1" || !mod.has_play()) throw openrac::Error("Better Pause Menu requires RAC1 play API");
    g=&mod;

    hub=mod.ui_page([&mod](const openrac::UiPage& page,const openrac::UiEvent& event){
        const auto status=mod.play_status();
        openrac_pose pose{}; pose.size=sizeof pose;
        const bool hero=mod.hero_pose(pose);
        openrac::UiContent c;
        c.title="QoL Hub";
        c.items={
            openrac::UiItem::text(status.level>=0 ? "Planet: "+mod.level_name(status.level) : "Planet: -"),
            openrac::UiItem::text(qol::hp_line(hero?pose.health:0)),
            openrac::UiItem::button("Restart current planet",status.level>=0 && status.phase==OPENRAC_PLAY_PLAYING)
        };
        c.footer={"L1 + R1 + Up opens this page"};
        page.set(c);
        if(event.is(OPENRAC_UI_CHOOSE) && event.item()==2 && status.level>=0
           && status.phase==OPENRAC_PLAY_PLAYING) {
            mod.play_travel(status.level);
        }
    });

    if(mod.has_pause_items()){
        try {
            mod.ui_pause_item("QoL Hub",hub);
        } catch(const openrac::Error&) {
            mod.info("Pause slots full; QoL Hub remains available by hotkey");
        }
    }

    mod.on_pad([](int port,std::uint8_t* report,int size){
        if(port!=0 || report==nullptr || size<2) return;
        const auto now=qol::pad_buttons(report);
        const auto show=static_cast<std::uint16_t>(OPENRAC_PAD_L1|OPENRAC_PAD_R1|OPENRAC_PAD_UP);
        const auto restart=static_cast<std::uint16_t>(OPENRAC_PAD_L1|OPENRAC_PAD_R1|OPENRAC_PAD_DOWN);
        if(qol::combo_pressed(now,previous,show)) action=1;
        if(qol::combo_pressed(now,previous,restart)) action=2;
        if((now&show)==show) qol::consume_buttons(report,show);
        if((now&restart)==restart) qol::consume_buttons(report,restart);
        previous=now;
    });

    mod.on_frame([]{
        if(action==0) return;
        const int a=action; action=0;
        const auto status=g->play_status();
        if(a==1){ hub.open(); return; }
        if(a==2 && status.level>=0 && status.phase==OPENRAC_PLAY_PLAYING) g->play_travel(status.level);
    });
}
OPENRAC_MOD_CPP("better-pause-menu","0.2.0",load)
