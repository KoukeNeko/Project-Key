"""Prints the text column of a Wikipedia parquet shard, one paragraph per line.

Usage: python extract.py <shard.parquet> <max characters>
"""
import sys

import pyarrow.parquet as pq


def main():
    shard, limit = sys.argv[1], int(sys.argv[2])
    written = 0
    for batch in pq.ParquetFile(shard).iter_batches(columns=["text"], batch_size=1000):
        for text in batch.column(0).to_pylist():
            for paragraph in text.split("\n"):
                if paragraph:
                    sys.stdout.write(paragraph + "\n")
                    written += len(paragraph)
            if written >= limit:
                return


if __name__ == "__main__":
    main()
