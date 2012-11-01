/* 
 * RGB2R-C128-Kassenprogramm
 * © 2007-2009 phil_fry, sECuRE, sur5r
 * See LICENSE for license information
 *
 */
#define _IS_KASSE
#include <stdio.h>
#include <conio.h>
#include <stdlib.h>
#include <string.h>
#include <cbm.h>

#include "general.h"
#include "config.h"
#include "kasse.h"
#include "credit_manager.h"
#include "c128time.h"
#include "print.h"
// drucker 4 oder 5
// graphic 4,0,10

static void sane_exit() {
	save_items();
	save_credits();
	exit(1);
}

/* Hauptbildschirm ausgeben */
static void print_screen() {
	BYTE i = 0;
	char *time = get_time();
	char profit[10];
	clrscr();
	if (format_euro(profit, 10, money) == NULL) {
		cprintf("Einnahme %ld konnte nicht umgerechnet werden\r\n", money);
		exit(1);
	}
	textcolor(TC_CYAN);
	cprintf("C128-Kassenprogramm (phil_fry, sECuRE, sur5r) " GV "\r");
	textcolor(TC_LIGHT_GRAY);
	cprintf("\
\r\nUhrzeit:     %s (wird nicht aktualisiert)\r\
Eingenommen: %s, Verkauft: %ld Dinge, Drucken: %s\r\n",
	time, profit, items_sold, (printing == 1 ? "ein" : "aus"));
	textcolor(TC_LIGHT_GRAY);
	cprintf("      \xB0\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\xB2\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\xB2\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\xB2\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\xAE\r\n");
	for (; i < min(status.num_items, 15); ++i) {
		if (format_euro(profit, sizeof(profit), status.status[i].price) == NULL) {
			cprintf("Preis %ld konnte nicht umgerechnet werden\r\n", status.status[i].price);
			exit(1);
		}

		cprintf("      \x7D");
		textcolor(TC_YELLOW);
		cprintf("%2d", i);
		textcolor(TC_LIGHT_GRAY);
		cprintf(": %-" xstr(MAX_ITEM_NAME_LENGTH) "s \x7D%s, %3dx \x7D",
			status.status[i].item_name, profit, status.status[i].times_sold);
		if ((i+15) < status.num_items) {

			if (format_euro(profit, sizeof(profit), status.status[i+15].price) == NULL) {
				cprintf("Preis %ld konnte nicht umgerechnet werden\r\n", status.status[i+15].price);
				exit(1);
			}
			textcolor(TC_YELLOW);
			cprintf("%2d", i+15);
			textcolor(TC_LIGHT_GRAY);
			cprintf(": %-" xstr(MAX_ITEM_NAME_LENGTH) "s \x7D%s, %3dx \x7D",
				status.status[i+15].item_name, profit, status.status[i+15].times_sold);
		} else cprintf("              \x7D                \x7D");
		cprintf("\r\n");
	}
	cprintf("      \xAD\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\xB1\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\xB1\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\xB1\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\x60\xBD\r\n");
	textcolor(TC_YELLOW);
	cprintf("   s");
	textcolor(TC_LIGHT_GRAY);
	cprintf(") Daten sichern                                  ");
	textcolor(TC_YELLOW);
	cprintf("g");
	textcolor(TC_LIGHT_GRAY);
	cprintf(") Guthabenverwaltung\r\n");
	textcolor(TC_YELLOW);
	cprintf("   z");
	textcolor(TC_LIGHT_GRAY);
	cprintf(") Zeit setzen         ");
	textcolor(TC_YELLOW);
	cprintf("f");
	textcolor(TC_LIGHT_GRAY);
	cprintf(") Freitext verkaufen      ");
	textcolor(TC_YELLOW);
	cprintf("q");
	textcolor(TC_LIGHT_GRAY);
	cprintf(") Beenden\r\n");
}

/*
 * Prints a line and logs it to file. Every line can be at max 80 characters.
 *
 */
static void print_log(char *name, int item_price, int einheiten, char *nickname, char *rest) {
	char *time = get_time();
	char price[10];
	/* Format: 
	   Transaction-ID (Anzahl verkaufter Einträge, inklusive des zu druckenden!) -- 6-stellig
	   Uhrzeit -- 8-stellig
	   Eintragname (= Getränk) -- 9-stellig
	   Preis (in Cents) -- 9-stellig
	   Anzahl -- 2-stellig
	   Nickname (falls es vom Guthaben abgezogen wird) -- 10-stellig
	   restguthaben (9-stellig)

	   + 7 leerzeichen
	   --> 48 zeichen
	   */
	if (format_euro(price, 10, item_price) == NULL) {
		cprintf("Preis %d konnte nicht umgerechnet werden\r\n", item_price);
		exit(1);
	}

	sprintf(print_buffer, "%c[%3u] %s - %-" xstr(MAX_ITEM_NAME_LENGTH) "s - %s - %s - %d - an %s\r",  17,
			status.transaction_id, time, name, price, rest,
			einheiten, (*nickname != '\0' ? nickname : "Unbekannt"));
	status.transaction_id++;
	print_the_buffer();
}

/* dialog which is called for each bought item */
static signed int buy(char *name, unsigned int price) {
	int negative = 1;
	char entered[5] = {'1', 0, 0, 0, 0};
	BYTE i = 0, matches = 0;
	BYTE c, x, y, nickname_len;
	int einheiten;
	char *input;
	char nickname[11];
	char rest[11];
	struct credits_t *credit;

	memset(rest, ' ', sizeof(rest));
	rest[8] = '\0';

	clrscr();
	cprintf("Wieviel Einheiten \"%s\"? [1] \r\n", name);
	x = wherex();
	y = wherey();
	while (1) {
		/* Buffer-Ende erreicht? */
		if (i == 4)
			break;

		c = cgetc();
		/* Enter */
		if (c == 13)
			break;
		/* Backspace */
		if (c == 20) {
			if (i == 0)
				continue;
			entered[--i] = '\0';
			cputcxy(x+i, y, ' ');
			gotoxy(x+i, y);
			continue;
		}
		if (c == 27) {
			cprintf("Kauf abgebrochen, druecke RETURN...\r\n");
			get_input();
			return 1;
		}
		if (c == '-' && i == 0) {
			negative = -1;
			cputc(c);
		} else if (c > 47 && c < 58) {
			entered[i++] = c;
			cputc(c);
		}

		/* Ungültige Eingabe (keine Ziffer), einfach ignorieren */
	}
	einheiten = atoi(entered) * negative;

	if (einheiten > 100 || einheiten < -100 || einheiten == 0) {
		cprintf("\r\nEinheit nicht in [-100, 100] oder 0, Abbruch, druecke RETURN...\r\n");
		cgetc();
		return 1;
	}
	
	toggle_videomode();
	cprintf("\r\n             *** VERKAUF ***\r\n\r\n");
	cprintf("%dx %s", einheiten, name);
	toggle_videomode();
	
	cprintf("\r\nAuf ein Guthaben kaufen? Wenn ja, Nickname eingeben:\r\n");
	input = get_input();
	strncpy(nickname, input, 11);
	if (*nickname != '\0') {
		toggle_videomode();
		cprintf(" fuer %s\r\n", nickname);
		toggle_videomode();
	}

	if (*nickname != '\0' && *nickname != 32) {
		nickname_len = strlen(nickname);
		/* go through credits and remove the amount of money or set nickname
		 * to NULL if no such credit could be found */
		credit = find_credit(nickname);
		if (credit != NULL) {
			while ((signed int)credit->credit < ((signed int)price * einheiten)) {
				if (format_euro(rest, 10, credit->credit) == NULL) {
					cprintf("Preis %d konnte nicht umgerechnet werden\r\n", credit->credit);
					exit(1);
				}
				cprintf("\r\n%s hat nicht genug Geld (%s). e) einzahlen a) abbruch \r\n", nickname, rest);
				c = cgetc();
				if (c == 'e') {
					deposit_credit(nickname);
				} else {
					return 0;
				}
			}
			/* substract money */
			credit->credit -= (price * einheiten);

			if (format_euro(rest, 10, credit->credit) == NULL) {
				cprintf("Preis %d konnte nicht umgerechnet werden\r\n", credit->credit);
				exit(1);
			}

			textcolor(TC_LIGHT_GREEN);
			cprintf("\r\nVerbleibendes Guthaben fuer %s: %s. Druecke RETURN...\r\n",
				nickname, rest);
			textcolor(TC_LIGHT_GRAY);
			toggle_videomode();
			cprintf("\r\nDein Guthaben betraegt noch %s.\r\n", rest);
			toggle_videomode();
			get_input();
			matches++;
		} else {
			textcolor(TC_LIGHT_RED);
			cprintf("\r\nNickname nicht gefunden in der Guthabenverwaltung! Abbruch, druecke RETURN...\r\n");
			textcolor(TC_LIGHT_GRAY);
			get_input();
			return 0;
		}
	} else {
		/* Ensure that nickname is NULL if it's empty because it's used in print_log */
		*nickname = '\0';
	}
	
	money += price * einheiten;
	items_sold += einheiten;
	if (printing == 1)
		print_log(name, price, einheiten, nickname, rest);

	return einheiten;
}

void buy_stock(BYTE n) {
	if (n >= status.num_items || status.status[n].item_name == NULL) {
		cprintf("FEHLER: Diese Einheit existiert nicht.\r\n");
		get_input();
		return;
	}

	status.status[n].times_sold += buy(status.status[n].item_name, status.status[n].price);
}

void buy_custom() {
	BYTE c = 0, i = 0;
	int negative = 1;
	char entered[5] = {'1', 0, 0, 0, 0};
	char *input, name[20];
	int price;

	clrscr();
	memset(name, '\0', 20);
	cprintf("\r\nWas soll gekauft werden?\r\n");
	input = get_input();
	strncpy(name, input, 20);
	if (*name == '\0')
		return;

	cprintf("\r\nWie teuer ist \"%s\" (in cents)?\r\n", name);
	while (1) {
		c = cgetc();
		if (c == 13)
			break;
		cputc(c);
		if (c == 27) {
			cprintf("Kauf abgebrochen, druecke RETURN...\r\n");
			get_input();
			return;
		} else if (c == '-' && i == 0)
			negative = -1;
		else if (c > 47 && c < 58)
			entered[i++] = c;
	}
	price = atoi(entered) * negative;

	cprintf("\r\n");

	buy(name, price);
}

void set_time_interactive() {
	BYTE part[3] = {'0', '0', '\0'};
	BYTE tp1, tp2, tp3;
	char *time_input, *time;
	cprintf("Gib die aktuelle Uhrzeit ein (Format HHMMSS):\r\n");
	time_input = get_input();
	part[0] = time_input[0];
	part[1] = time_input[1];
	tp1 = atoi(part);
	part[0] = time_input[2];
	part[1] = time_input[3];
	tp2 = atoi(part);
	part[0] = time_input[4];
	part[1] = time_input[5];
	tp3 = atoi(part);
	set_time(tp1, tp2, tp3);

	time = get_time();
	cprintf("\r\nZeit gesetzt: %s\r\n", time);
}

int main() {
	char *c;
	char *time;

	if (VIDEOMODE == 40)
		toggle_videomode();
	clrscr();

	/* Allocate logging buffer memory */
	init_log();

	/* Set time initially, c128 doesn't know it */
	set_time_interactive();

	POKE(216, 255);

	/* Load configuration */
	load_config();

	/* Load items (= drinks) */
	load_items();
	/* Load credits */
	load_credits();

	time = get_time();
	sprintf(print_buffer, "%c--------------------------------------------------------------------------------\r", 17);
	print_the_buffer();
	sprintf(print_buffer, "%cC128-Kasse Version " GV "\r", 17);
	print_the_buffer();

	sprintf(print_buffer, "%cKasse gestartet um %s. Nutze logfile log-%u, offset %d.\r", 17, time, log_num, log_heap_offset);
	print_the_buffer();

	print_header();

	while (1) {
		print_screen();
		c = get_input();
		/* ...display dialogs eventually */
		if (*c > 47 && *c < 58) {
			/* if the input starts with a digit, we will interpret it as a number
			 * for the item to be sold */
			buy_stock(atoi(c));
			toggle_videomode();
			clrscr();
			toggle_videomode();
		} else if (*c == 'f') {
			buy_custom();
			toggle_videomode();
			clrscr();
			toggle_videomode();
		} else if (*c == 's') {
			save_items();
			save_credits();
			log_flush();
			cprintf("\r\nStatefile/Creditfile/Log gesichert, druecke RETURN...\r\n");
			get_input();
		} else if (*c == 'g') {
			credit_manager();
		} else if (*c == 'z') {
			set_time_interactive();
		} else if (*c == 'q')
			break;
	}
	clrscr();
	cprintf("\r\nBYEBYE\r\n");

	return 0;
}
