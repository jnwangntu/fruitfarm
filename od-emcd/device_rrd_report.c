#include <unistd.h>
#include "emc.h"
#include "device.h"
#include "devtypes.h"
#include "device_report.h"
#include "dbac_report.h"

/*
 * Internal debugging section
 */
//static int _dev_rrd_report_loglv = DEV_REPORT_LOGLV;
//static int _dev_rrd_report_dbglv = DEV_REPORT_DBGLV;

#if defined(DEV_RRD_REPORT_LOGLV) && DEV_RRD_REPORT_LOGLV > 0
#else
#  undef ciplog
#  define ciplog(x...)
#endif

#if defined(DEV_RRD_REPORT_DBGLV) && DEV_RRD_REPORT_DBGLV > 0
#else
#  undef cipdbg
#  define cipdbg(x...)
#endif


#define _dev_rrd_report_dbg(l, fmt...) \
({ \
    cipdbg(__FILE__ ":", _dev_rrd_report_dbglv, CDBG_ ## l, fmt) ; \
    ciplog(__FILE__ ":", _dev_rrd_report_loglv, CDBG_ ## l, fmt) ; \
})

#define _dev_rrd_report_error(fmt...)     _dev_rrd_report_dbg(ERROR, fmt)
#define _dev_rrd_report_warn(fmt...)      _dev_rrd_report_dbg(WARN, fmt)
#define _dev_rrd_report_mdump(fmt...)     _dev_rrd_report_dbg(MDUMP, fmt)
#define _dev_rrd_report_trace(fmt...)     _dev_rrd_report_dbg(TRACE, fmt)
#define UTRACE(fmt...)         _dev_rrd_report_trace(fmt)

extern struct config *od_emcd_conf;


/*************         locally used function        *****************/
void test_and_create_rrd(const char *rrd_path, const char *type)
{
    char cmd[512];
    if (access(rrd_path, F_OK) == -1) {
        sprintf(cmd, "/ramfs/od_emcd/bin/rrd_create.sh %s %s", rrd_path, type);
        system(cmd);
    }
}
/*************         locally used function  END      *****************/

int rrd_dev_report_update(DEV_INFO *dev)
{
    int rfcard_num=0;
    int station_num=0;
    SYS_INFO *sys_info=NULL;
    RFCARD_INFO *rfcard_info=NULL;
    STATION_INFO *station_info=NULL;
    char cmd[1024], rrd_path[128], rrd_dev_dir[128];
    char buffVAP[5000];
    char buffALL[10000];
    int ret = 0;
    int i, j, prevapid;
    double network_delay=0;
    int station_good=0, station_ok=0, station_poor=0;
    unsigned long long tx_byte=0, rx_byte=0, tx_packet=0, rx_packet=0, tx_err_packet=0, rx_err_packet=0;

    FUNC_ENTER;

    /********************   Update System RRD   ******************/	
    sys_info = dbac_report_get_sys_info(dev->dev_id);
    if(!sys_info){
        ret = -1;
        goto exit;
    }
    
    sys_info->report_time = sys_info->report_time - sys_info->report_time % 60;
    // get cpu_usage
    if (sys_info->cpu_idle >= 0 && sys_info->cpu_idle <= 100)
        sys_info->cpu_idle = 100 - sys_info->cpu_idle;
    else
        sys_info->cpu_idle = 0;

    station_info = dbac_report_get_station_info(dev->dev_id, &station_num);
    // get station categories
    for (i = 0; i < station_num; i++) {
        if (station_info[i].snr < 60)
            station_poor++;
        else if(station_info[i].snr < 80)
            station_ok++;
        else
            station_good++;
    }

    // get network delay
    {
        FILE *fp = NULL;
        char buf[512], *p, *q;
        sprintf(cmd, "ping -c 1 %s", dev->ip);
        if((fp = popen(cmd, "r")) != NULL){
            while(fgets(buf, sizeof(buf), fp)){
                if( (p = strstr(buf, "rtt"))
                    && (p = strstr(p, "="))
                    && (p = strstr(p, "/"))
                    && (++p)
                    && (q = strstr(p, "/")) )
                {
                    *q = '\0';
                    network_delay = atof(p);
                    break;
                }
            }
            pclose(fp);
        }
    }

    /********************   Update VAP and WDS RRD   ******************/
    rfcard_info = dbac_report_get_rfcard_info(dev->dev_id, &rfcard_num);
    
    sprintf(rrd_dev_dir, "%s/%d", od_emcd_conf->rrd_dev_path, dev->dev_id);
    memset(buffVAP, 0, sizeof(buffVAP));
    prevapid = 0;

    for (i = 0; i < 2; i++) {
        // update vap info
        if (strstr(rfcard_info[i].mode,"ap") && rfcard_info[i].vap_num > 0 && rfcard_info[i].vap_info) {
            for (j = 0; j < rfcard_info[i].vap_num; j++) {
                VAP_INFO *vap = &rfcard_info[i].vap_info[j];
                if (prevapid < vap->id) {
                    for (; prevapid < vap->id; prevapid++) {
                        strcat(buffVAP,":0:0:0:0:0:0");
                    }
                }
                memset(cmd, 0, sizeof(cmd));
                sprintf(cmd, ":%llu:%llu:%llu:%llu:%llu:%llu",vap->tx_byte, vap->rx_byte, vap->tx_packet, vap->rx_packet, vap->tx_err_packet, vap->rx_err_packet);
                strcat(buffVAP, cmd);
                tx_byte += vap->tx_byte;
                rx_byte += vap->rx_byte;
                tx_packet += vap->tx_packet;
                rx_packet += vap->rx_packet;
                tx_err_packet += vap->tx_err_packet;
                rx_err_packet += vap->rx_err_packet;
                prevapid = vap->id + 1;
            }
            for (;prevapid < 16; prevapid++) {
                strcat(buffVAP,":0:0:0:0:0:0");
            }
        }
        else {
            strcat(buffVAP,":0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0");
        }
        // update for WDS (old school method)
        if (strstr(rfcard_info[i].mode,"wds") && rfcard_info[i].wds_num > 0 && rfcard_info[i].wds_info) {
            for (j = 0; j < rfcard_info[i].wds_num; j++) {
                WDS_INFO *wds = &rfcard_info[i].wds_info[j];
                char mac[REPORT_ITEM_LENGTH+1];
                {
                    // replace ':' to '-'
                    int k;
                    strncpy(mac, wds->link_mac, REPORT_ITEM_LENGTH);
                    mac[REPORT_ITEM_LENGTH]='\0';
                    for(k=0; k<strlen(mac); k++) if(mac[k] == ':') mac[k] = '-';
                }
                
                sprintf(rrd_path, "%s/%d_wds_%s.rrd", rrd_dev_dir, rfcard_info[i].id, mac);
                test_and_create_rrd(rrd_path, "wds");
                memset(cmd, 0, sizeof(cmd));
                sprintf(cmd, "rrdtool update %s %ld:%d:%d:%llu:%llu:%llu:%llu:%llu:%llu", rrd_path, sys_info->report_time, wds->rate, wds->snr, wds->tx_byte, wds->rx_byte, wds->tx_packet, wds->rx_packet, wds->tx_err_packet, wds->rx_err_packet);
                system(cmd);
            }
        } 
    }

    sprintf(rrd_path, "%s/all.rrd", rrd_dev_dir);
    test_and_create_rrd(rrd_path, "all");
    memset(buffALL, 0, sizeof(buffALL));
    sprintf(buffALL, "rrdtool update %s %ld:%d:%d:%d:%d:%d:%lf:%llu:%llu:%llu:%llu:%llu:%llu%s",
            rrd_path, sys_info->report_time, sys_info->cpu_idle, sys_info->memory_usage,
            station_good, station_ok, station_poor, network_delay,
            tx_byte, rx_byte, tx_packet, rx_packet, tx_err_packet, rx_err_packet, buffVAP);
    system(buffALL);

exit:
    free_sys_info(sys_info);
    if(rfcard_num>0)
        free_rfcard_info_all(rfcard_info, rfcard_num);
    free_station_info(station_info);

    _RETURN(ret);
}
