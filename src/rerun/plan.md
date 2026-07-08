
# goal:  fine tune pipeline config on real world record data

goal rerun processing with new pipeline on debug data

## structure of record
file name:
file-rec-rec-1783432744349-4683.tar.gz
unpacked:
file-rec-rec-1783432744349-4683/
├── frames
│   ├── frame_000000_7921485490700.jpg
│   ├── frame_000001_7921846815300.jpg
│   ├── frame_000002_7922208309200.jpg
│   ├── frame_005006_9730926809800.jpg
│   └── frame_005007_9731288325600.jpg
├── others
│   ├── debug
│   ├── log.txt
│   ├── metadata_log.txt
│   └── result_logger_summary.txt
├── pipeline-rec-1783432744349-4683.json
├── record-rec-1783432744349-4683.json
└── study-rec-1783432744349-4683.json


# steps 

* unpack tar file 
* remove old tar.gz file 
* mv old processing data (everything beside frames folder and json files to oldrun1 folder (or oldrun2 folder))
* do rerun processing with new pipeline
* repack tar.gz file