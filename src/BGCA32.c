#include <exec/types.h>
#include <exec/memory.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/view.h>
#include <intuition/intuition.h>
#include <hardware/custom.h>
#include <hardware/cia.h>
#include <string.h>
#include <stdio.h>

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 256
#define MAX_HEALTH    16

struct Library *GfxBase = NULL;
struct Library *IntuitionBase = NULL;

// Donanım Seviyesi Joystick / CD32 Gamepad & Ateş Tuşu Kayıtları
volatile UWORD *joy1dat = (volatile UWORD *)0xDFF00C; 
volatile UBYTE *ciaa_pra = (volatile UBYTE *)0xBFE001; 

typedef struct {
    WORD x, y;
    WORD health;
    ULONG score;
    UBYTE facing_right;
} BeePlayer;

typedef struct {
    WORD x, y;
    WORD type; // 100: Külçe, 50: Sikke, 25: Nugget, 10: Küçük
    UBYTE active;
} GoldItem;

typedef struct {
    WORD x, y;
    UBYTE active;
} FlyEnemy;

// Culling Optimizasyonu
void UpdateWorldCulling(BeePlayer *player, GoldItem *goldList, int totalGold, FlyEnemy *flyList, int totalFlies) {
    WORD min_x = player->x - 160;
    WORD max_x = player->x + 320;
    int i;

    for (i = 0; i < totalGold; i++) {
        goldList[i].active = (goldList[i].x >= min_x && goldList[i].x <= max_x) ? 1 : 0;
    }
    for (i = 0; i < totalFlies; i++) {
        flyList[i].active = (flyList[i].x >= min_x && flyList[i].x <= max_x) ? 1 : 0;
    }
}

// Piksel Art Çizim Rutinleri
void DrawPixelArtBee(struct RastPort *rp, WORD x, WORD y, UBYTE facing) {
    SetAPen(rp, 1); 
    WritePixel(rp, x + 4, y + 10); WritePixel(rp, x + 4, y + 11);
    WritePixel(rp, x + 8, y + 10); WritePixel(rp, x + 8, y + 11);
    if (facing) {
        WritePixel(rp, x, y + 6); WritePixel(rp, x + 1, y + 6); 
    } else {
        WritePixel(rp, x + 14, y + 6); WritePixel(rp, x + 15, y + 6); 
    }

    SetAPen(rp, 3); RectFill(rp, x + 2, y + 3, x + 13, y + 9); 
    SetAPen(rp, 1); RectFill(rp, x + 5, y + 3, x + 6, y + 9);  
    SetAPen(rp, 1); RectFill(rp, x + 9, y + 3, x + 10, y + 9); 

    WORD hx = facing ? (x + 11) : (x + 2);
    SetAPen(rp, 2); WritePixel(rp, facing ? (hx + 2) : hx, y + 5); 
    SetAPen(rp, 1); 
    WritePixel(rp, hx + 1, y + 2);
    WritePixel(rp, facing ? (hx + 2) : hx, y + 1);

    SetAPen(rp, 4); 
    RectFill(rp, x + 4, y + 1, x + 9, y + 2);
    WritePixel(rp, x + 5, y); WritePixel(rp, x + 8, y);
}

void DrawPixelArtFly(struct RastPort *rp, WORD x, WORD y) {
    SetAPen(rp, 1); RectFill(rp, x + 2, y + 2, x + 10, y + 8); 
    SetAPen(rp, 2); WritePixel(rp, x + 3, y + 4); WritePixel(rp, x + 3, y + 6); 
    SetAPen(rp, 4); WritePixel(rp, x + 5, y + 1); WritePixel(rp, x + 7, y + 1); 
    SetAPen(rp, 1); WritePixel(rp, x + 4, y + 9); WritePixel(rp, x + 8, y + 9); 
}

void DrawGold(struct RastPort *rp, WORD x, WORD y, WORD type) {
    SetAPen(rp, 3); 
    if (type == 100) {
        RectFill(rp, x, y, x + 12, y + 6); 
        SetAPen(rp, 4); WritePixel(rp, x + 2, y + 1); WritePixel(rp, x + 3, y + 1);
    } else if (type == 50) {
        RectFill(rp, x + 2, y, x + 7, y + 6); 
    } else {
        RectFill(rp, x + 1, y + 1, x + 5, y + 5); 
    }
}

void DrawEnvironment(struct RastPort *rp, WORD camera_x) {
    SetAPen(rp, 0);
    RectFill(rp, 0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);

    SetAPen(rp, 4);
    WORD cloud1_x = (320 - ((camera_x / 2) % 360));
    RectFill(rp, cloud1_x, 35, cloud1_x + 28, 43);

    SetAPen(rp, 2); 
    RectFill(rp, 0, SCREEN_HEIGHT - 38, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 32);
    SetAPen(rp, 1); 
    RectFill(rp, 0, SCREEN_HEIGHT - 32, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 20);
}

void DrawHUD(struct RastPort *rp, BeePlayer *player, ULONG health_timer, ULONG high_score) {
    char hud_buf[64];
    ULONG sec_left = (500 - health_timer) / 50; 

    SetAPen(rp, 4);
    sprintf(hud_buf, "SKOR:%lu  EN YUKSEK:%lu  +2 CAN:%lus", player->score, high_score, sec_left);
    Move(rp, 8, 12);
    Text(rp, hud_buf, strlen(hud_buf));

    SetAPen(rp, 0);
    RectFill(rp, 0, SCREEN_HEIGHT - 19, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);
    SetAPen(rp, 3);
    sprintf(hud_buf, "CAN (STACK): %d / %d  [P: PAUSE]", player->health, MAX_HEALTH);
    Move(rp, 8, SCREEN_HEIGHT - 6);
    Text(rp, hud_buf, strlen(hud_buf));
}

void ReadHardwareGamepad(BeePlayer *player) {
    UWORD joy = *joy1dat;
    BOOL right = (joy & 0x0002) != 0;
    BOOL left  = (joy & 0x0200) != 0;
    BOOL down  = ((joy >> 1) ^ joy) & 0x0001;
    BOOL up    = ((joy >> 1) ^ joy) & 0x0100;

    if (up && player->y > 20) player->y -= 3;
    if (down && player->y < SCREEN_HEIGHT - 52) player->y += 3;
    if (left) { player->x -= 2; player->facing_right = 0; }
    if (right) { player->x += 2; player->facing_right = 1; }
}

int main(int argc, char **argv) {
    GfxBase = OpenLibrary("graphics.library", 33L);
    IntuitionBase = OpenLibrary("intuition.library", 33L);

    if (!GfxBase || !IntuitionBase) {
        if (IntuitionBase) CloseLibrary(IntuitionBase);
        if (GfxBase) CloseLibrary(GfxBase);
        return 20;
    }

    struct NewScreen myScreen = {
        0, 0,                   
        SCREEN_WIDTH, SCREEN_HEIGHT, 4, 
        0, 1,                   
        0,                      
        CUSTOMSCREEN | SCREENQUIET, 
        NULL,                   
        (UBYTE *)"BEE GOLD COLLECTOR: AMIGA CD32 / CDTV", 
        NULL,                   
        NULL                    
    };

    struct Screen *screen = OpenScreen(&myScreen);
    if (!screen) {
        CloseLibrary(IntuitionBase);
        CloseLibrary(GfxBase);
        return 20;
    }

    // Renk Paleti Ataması
    SetRGB4(&screen->ViewPort, 0, 0, 0, 0);   
    SetRGB4(&screen->ViewPort, 1, 2, 2, 2);   
    SetRGB4(&screen->ViewPort, 2, 15, 2, 2);  
    SetRGB4(&screen->ViewPort, 3, 15, 13, 2); 
    SetRGB4(&screen->ViewPort, 4, 13, 15, 15);

    struct NewWindow myWindow = {
        0, 0,                   
        SCREEN_WIDTH, SCREEN_HEIGHT, 
        0, 1,                   
        IDCMP_RAWKEY | IDCMP_MOUSEMOVE | IDCMP_MOUSEBUTTONS, 
        WFLG_BACKDROP | WFLG_BORDERLESS | WFLG_ACTIVATE | WFLG_REPORTMOUSE, 
        NULL,                   
        NULL,                   
        (UBYTE *)"BGC_A32",     
        screen,                 
        NULL,                   
        0, 0, 0, 0,             
        CUSTOMSCREEN            
    };

    struct Window *window = OpenWindow(&myWindow);
    if (!window) {
        CloseScreen(screen);
        CloseLibrary(IntuitionBase);
        CloseLibrary(GfxBase);
        return 20;
    }

    // KRİTİK CHIP RAM KURALI: Agnus'un okuyabilmesi için boş imleç verisi Chip RAM'e tahsis edilir
    UWORD *blank_pointer = (UWORD *)AllocMem(12, MEMF_CHIP | MEMF_CLEAR);
    if (blank_pointer) {
        SetPointer(window, blank_pointer, 1, 16, 0, 0);
    }

    struct RastPort *rp = window->RPort;

    BeePlayer player = {100, 120, 6, 0, 1};
    ULONG health_timer = 0;
    ULONG high_score = 1250;
    ULONG last_score = 0;
    UBYTE game_state = 0; 
    BOOL running = TRUE;
    BOOL state_changed = TRUE;
    int i;

    GoldItem goldList[6] = {
        {180, 75, 100, 1},
        {240, 115, 50, 1},
        {310, 85, 25, 1},
        {380, 145, 10, 1},
        {460, 65, 100, 1},
        {520, 120, 50, 1}
    };

    FlyEnemy flyList[3] = {
        {220, 95, 1},
        {350, 125, 1},
        {490, 80, 1}
    };

    struct IntuiMessage *msg;
    char score_text[40];

    while (running) {
        // CD32 / CDTV Gamepad Ateş Tuşu ile Menü Başlatma
        if (game_state == 0 && !(*ciaa_pra & 0x80)) {
            player.x = 100; player.y = 120;
            player.health = 6; player.score = 0;
            health_timer = 0;
            for (i = 0; i < 6; i++) { goldList[i].active = 1; goldList[i].x = 180 + (i * 70); }
            game_state = 1;
            state_changed = TRUE;
        }

        while ((msg = (struct IntuiMessage *)GetMsg(window->UserPort))) {
            ULONG class = msg->Class;
            UWORD code = msg->Code;
            WORD mouse_x = msg->MouseX;
            WORD mouse_y = msg->MouseY;
            ReplyMsg((struct Message *)msg);

            if (class == IDCMP_RAWKEY) {
                if (code == 0x45) { running = FALSE; }

                if (game_state == 0) { 
                    if (code == 0x01) { 
                        player.x = 100; player.y = 120;
                        player.health = 6; player.score = 0;
                        health_timer = 0;
                        for (i = 0; i < 6; i++) { goldList[i].active = 1; goldList[i].x = 180 + (i * 70); }
                        game_state = 1;
                        state_changed = TRUE;
                    } else if (code == 0x02) {
                        game_state = 2;
                        state_changed = TRUE;
                    }
                }
                else if (game_state == 1) {
                    if (code == 0x19) { game_state = 3; state_changed = TRUE; }
                    else if (code == 0x4C && player.y > 20) player.y -= 6;
                    else if (code == 0x4D && player.y < SCREEN_HEIGHT - 52) player.y += 6;
                    else if (code == 0x4F) { player.x -= 6; player.facing_right = 0; }
                    else if (code == 0x4E) { player.x += 6; player.facing_right = 1; }
                }
                else if (game_state == 2) {
                    if (code == 0x01) { game_state = 0; state_changed = TRUE; }
                }
                else if (game_state == 3) {
                    if (code == 0x01) { game_state = 1; state_changed = TRUE; }
                    else if (code == 0x02) { 
                        last_score = player.score;
                        if (last_score > high_score) high_score = last_score;
                        game_state = 0; 
                        state_changed = TRUE; 
                    }
                }
            }
            else if (game_state == 1 && class == IDCMP_MOUSEMOVE) {
                if (mouse_y >= 20 && mouse_y <= SCREEN_HEIGHT - 52) player.y = mouse_y;
                player.facing_right = (mouse_x > 160) ? 1 : 0;
            }
        }

        if (game_state == 1) {
            ReadHardwareGamepad(&player);

            health_timer++;
            if (health_timer >= 500) {
                health_timer = 0;
                if (player.health + 2 <= MAX_HEALTH) {
                    player.health += 2;
                } else {
                    player.health = MAX_HEALTH;
                }
            }

            player.x += 1;
            UpdateWorldCulling(&player, goldList, 6, flyList, 3);
            DrawEnvironment(rp, player.x);

            WORD screen_bee_x = 80; 
            for (i = 0; i < 6; i++) {
                if (goldList[i].x < player.x - 160) {
                    goldList[i].x += 640;
                    goldList[i].active = 1;
                }
                if (goldList[i].active) {
                    WORD draw_x = (goldList[i].x - player.x) + screen_bee_x;
                    if (draw_x >= 0 && draw_x < SCREEN_WIDTH) {
                        DrawGold(rp, draw_x, goldList[i].y, goldList[i].type);
                        if (draw_x >= screen_bee_x - 8 && draw_x <= screen_bee_x + 14 &&
                            goldList[i].y >= player.y - 8 && goldList[i].y <= player.y + 12) {
                            player.score += goldList[i].type;
                            goldList[i].active = 0;
                            goldList[i].x += 640; 
                        }
                    }
                }
            }

            for (i = 0; i < 3; i++) {
                if (flyList[i].x < player.x - 160) {
                    flyList[i].x += 640;
                    flyList[i].active = 1;
                }
                if (flyList[i].active) {
                    WORD draw_x = (flyList[i].x - player.x) + screen_bee_x;
                    if (draw_x >= 0 && draw_x < SCREEN_WIDTH) {
                        DrawPixelArtFly(rp, draw_x, flyList[i].y);
                        if (draw_x >= screen_bee_x - 6 && draw_x <= screen_bee_x + 12 &&
                            flyList[i].y >= player.y - 6 && flyList[i].y <= player.y + 10) {
                            player.health -= 2;
                            flyList[i].x += 320; 
                            if (player.health <= 0) {
                                last_score = player.score;
                                if (last_score > high_score) high_score = last_score;
                                game_state = 0;
                                state_changed = TRUE;
                            }
                        }
                    }
                }
            }

            DrawPixelArtBee(rp, screen_bee_x, player.y, player.facing_right);
            DrawHUD(rp, &player, health_timer, high_score);
        }

        if (state_changed) {
            if (game_state == 0) {
                SetAPen(rp, 0); RectFill(rp, 0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);
                SetAPen(rp, 3); Move(rp, 45, 65); Text(rp, "BEE GOLD COLLECTOR: A32", 23);
                SetAPen(rp, 2);
                Move(rp, 85, 105); Text(rp, "1: OYUNA BASLA (FIRE)", 21);
                Move(rp, 85, 130); Text(rp, "2: SKORLAR MENUSU", 17);
                SetAPen(rp, 4);
                Move(rp, 35, 175); Text(rp, "KONTROL: JOYPAD / FARE / OK TUSLARI", 36);
                SetAPen(rp, 3); Move(rp, 210, 245); Text(rp, "2026 KGAMER", 11);
                DrawPixelArtBee(rp, 55, 100, 1);
                state_changed = FALSE;
            }
            else if (game_state == 2) {
                SetAPen(rp, 0); RectFill(rp, 0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);
                SetAPen(rp, 3); Move(rp, 95, 60); Text(rp, "SKORLAR TABLOSU", 15);
                SetAPen(rp, 2);
                sprintf(score_text, "EN YUKSEK SKOR: %lu", high_score);
                Move(rp, 75, 105); Text(rp, score_text, strlen(score_text));
                sprintf(score_text, "SON OYUN SKORU: %lu", last_score);
                Move(rp, 75, 130); Text(rp, score_text, strlen(score_text));
                SetAPen(rp, 4); Move(rp, 65, 190); Text(rp, "ANA MENUYE DONMEK ICIN 1'E BAS", 29);
                state_changed = FALSE;
            }
            else if (game_state == 3) {
                SetAPen(rp, 1); RectFill(rp, 55, 75, 265, 175);
                SetAPen(rp, 3); Move(rp, 100, 95); Text(rp, "OYUN DURAKLATILDI", 17);
                SetAPen(rp, 4);
                Move(rp, 80, 125); Text(rp, "1: OYUNA DEVAM ET", 17);
                Move(rp, 80, 150); Text(rp, "2: ANA MENUYE DON", 17);
                state_changed = FALSE;
            }
        }

        WaitTOF();
    }

    // Güvenli Kaynak Temizliği (Bellek Sızıntısı Yok)
    if (blank_pointer) {
        FreeMem(blank_pointer, 12);
    }
    CloseWindow(window);
    CloseScreen(screen);
    CloseLibrary(IntuitionBase);
    CloseLibrary(GfxBase);
    return 0;
}
