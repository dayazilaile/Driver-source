#include "../native/oem_catalog.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static OemCatalog catalog;
static const char test_rows[] =
 "a\tlenovo\tThinkPad Test\t20L5;20L6\twin10\t22H2\t2024-01-01\thttps://download.lenovo.com/a.exe\t5878e019324d3ecb57f60b46caa102e2ab01f560f8fef09e33ca31d87ef950a4\t0\texe-download\n"
 "b\tlenovo\tThinkPad Test\t20L5\twin10\t*\t2025-01-01\thttps://download.lenovo.com/b.exe\t5878e019324d3ecb57f60b46caa102e2ab01f560f8fef09e33ca31d87ef950a4\t0\texe-download\n"
 "c\tdell\tLatitude 5510\t0925\twin11\t*\t2023-01-01\thttps://downloads.dell.com/c.cab\t5878e019324d3ecb57f60b46caa102e2ab01f560f8fef09e33ca31d87ef950a4\t3200000000\tcab\n"
 "d\thp\tHP EliteBook Test\t0ABC;0ABD\twin11\t23H2\t2025-01-01\thttps://ftp.hp.com/pub/softpaq/sp1.exe\t5878e019324d3ecb57f60b46caa102e2ab01f560f8fef09e33ca31d87ef950a4\t100000\texe-download\n"
 "e\thp\tHP Other Model\t0ABC\twin11\t23H2\t2026-01-01\thttps://ftp.hp.com/pub/softpaq/sp2.exe\t5878e019324d3ecb57f60b46caa102e2ab01f560f8fef09e33ca31d87ef950a4\t100000\texe-download\n";
static void rejected(const char *text) {
 char copy[4096], error[128]; assert(strlen(text) < sizeof(copy)); strcpy(copy,text);
 assert(!oem_catalog_parse(&catalog,copy,error,sizeof(error))); assert(!catalog.count && error[0]);
}
int main(void) {
 char copy[4096], error[128]; strcpy(copy,test_rows);
 assert(oem_catalog_parse(&catalog,copy,error,sizeof(error)) && catalog.count == 5);
 assert(oem_catalog_select(&catalog,"LENOVO","20L5S01A00","",19045,1)==0);
 assert(oem_catalog_select(&catalog,"Lenovo","20L6S01A00","",19045,1)==0);
 assert(oem_catalog_select(&catalog,"Lenovo","ThinkPad Test","",19044,1)==1);
 assert(oem_catalog_select(&catalog,"Lenovo","20L5S01A00","",22631,1)==-1);
 assert(oem_catalog_select(&catalog,"Dell Inc.","Latitude 5510","0925",22631,1)==2);
 assert(oem_catalog_select(&catalog,"Dell Inc.","Latitude 5510","",26100,1)==2);
 assert(oem_catalog_select(&catalog,"Dell Inc.","Latitude 5510","FFFF",22631,1)==-1);
 assert(oem_catalog_select(&catalog,"Dell Inc.","Latitude 5510","0925",19045,1)==-1);
 assert(oem_catalog_select(&catalog,"Dell Inc.","Latitude 5510","0925",22631,0)==-1);
 assert(oem_catalog_select(&catalog,"Lenovo","20L5","",19045,0)==-1);
 assert(oem_catalog_select(&catalog,"Other","20L5","",19045,1)==-1);
 assert(oem_catalog_select(&catalog,"My Lenovo compatible PC","20L5","",19045,1)==-1);
 assert(oem_catalog_select(&catalog,"Lenovo","120L5","",19045,1)==-1);
 OemMachine hp = {"HP Inc.","HP EliteBook Test","5Q7X1EA#ABU","0abc",22631,1};
 assert(oem_catalog_select_machine(&catalog,&hp)==3);
 hp.manufacturer="Hewlett-Packard"; assert(oem_catalog_select_machine(&catalog,&hp)==3);
 hp.manufacturer="Hewlett-Packard Company"; assert(oem_catalog_select_machine(&catalog,&hp)==3);
 hp.baseboard="FFFF"; assert(oem_catalog_select_machine(&catalog,&hp)==-1);
 hp.baseboard="0ABD"; hp.system_sku="FFFF"; assert(oem_catalog_select_machine(&catalog,&hp)==3);
 hp.baseboard=""; assert(oem_catalog_select_machine(&catalog,&hp)==3);
 hp.baseboard="0ABC"; hp.product="Unknown HP Model"; assert(oem_catalog_select_machine(&catalog,&hp)==4);
 hp.build=19045; assert(oem_catalog_select_machine(&catalog,&hp)==-1);
 hp.build=22631; hp.native_x64=0; assert(oem_catalog_select_machine(&catalog,&hp)==-1);
 hp.native_x64=1; hp.manufacturer="Other manufacturer"; assert(oem_catalog_select_machine(&catalog,&hp)==-1);
 assert(oem_catalog_select_machine(&catalog,NULL)==-1);
 VendorPackage vendor;
 oem_package_as_vendor(&catalog.packages[0],&vendor); assert(vendor.format==VENDOR_EXE_DOWNLOAD && !vendor.bytes);
 oem_package_as_vendor(&catalog.packages[2],&vendor); assert(vendor.format==VENDOR_CAB_PACK && vendor.bytes==UINT64_C(3200000000));
 rejected("bad\trow\n");
 strcpy(copy,test_rows); char *url=strstr(copy,"download.lenovo.com"); url[0]='x'; rejected(copy);
 strcpy(copy,test_rows); char *hp_url=strstr(copy,"https://ftp.hp.com/pub/softpaq/"); hp_url[8]='x'; rejected(copy);
 strcpy(copy,test_rows); char *mode=strstr(copy,"exe-download"); mode[0]='c'; rejected(copy);
 FILE *file=fopen("database/oem-packages.tsv","rb"); assert(file);
 assert(fseek(file,0,SEEK_END)==0); long size=ftell(file); assert(size>0 && size<4194304); rewind(file);
 char *data=malloc((size_t)size+1); assert(data);
 assert(fread(data,1,(size_t)size,file)==(size_t)size); fclose(file); data[size]=0;
 assert(oem_catalog_parse(&catalog,data,error,sizeof(error)) && catalog.count>7000);
 size_t lenovo=0,dell=0,hp_count=0,cab=0;
 for(size_t i=0;i<catalog.count;++i) {
  const OemPackage *p=&catalog.packages[i];
  if(!strcmp(p->manufacturer,"lenovo"))++lenovo;
  else if(!strcmp(p->manufacturer,"dell"))++dell;
  else if(!strcmp(p->manufacturer,"hp"))++hp_count;
  if(p->format==VENDOR_CAB_PACK)++cab;
 }
 assert(lenovo>2000 && dell>600 && hp_count>3000 && cab>100);
 hp=(OemMachine){"HP","HP Collaboration G6 24 All-in-One with Zoom Rooms","product SKU","874e",22631,1};
 assert(oem_catalog_select_machine(&catalog,&hp)>=0);
 assert(oem_catalog_select(&catalog,"LENOVO","20L5000000","",19042,1)>=0);
 assert(oem_catalog_select(&catalog,"Unknown","20L5000000","",19045,1)==-1);
 printf("OEM catalog: %zu records, Lenovo %zu, Dell %zu, HP %zu, CAB %zu; manufacturer/model/SKU/baseboard/OS/architecture isolation passed\n",catalog.count,lenovo,dell,hp_count,cab);
 free(data);return 0;
}
