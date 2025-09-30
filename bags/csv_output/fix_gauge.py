import pandas as pd
import numpy as np
from typing import List, Tuple, Dict

def analyze_timestamp_patterns(timestamps: np.ndarray, window: int = 100):
    """
    Analyze timestamp progression patterns to understand normal behavior.
    """
    diffs = np.diff(timestamps)
    
    # Use rolling statistics to handle varying rates
    median_diff = np.median(diffs[:min(1000, len(diffs))])
    
    return {
        'median_diff': median_diff,
        'expected_range': (median_diff * 0.5, median_diff * 2.0)
    }


def detect_shift_points(timestamps: np.ndarray, sensitivity: float = 5.0):
    """
    Detect points where bit shifts likely occurred.
    Uses ratio-based detection to find sudden jumps.
    """
    diffs = np.diff(timestamps)
    
    # Filter out zero or negative differences
    valid_diffs = diffs[diffs > 0]
    median_diff = np.median(valid_diffs)
    
    # Look for jumps that are way too large or small
    # A bit shift typically causes orders of magnitude change
    ratios = diffs / median_diff
    
    # Detect anomalies: ratios that are extremely large or small
    # Bit shifts cause ratios like 2^n or 1/2^n
    shift_points = []
    
    for i in range(len(ratios)):
        # Check if this difference is suspiciously different
        if ratios[i] > sensitivity or ratios[i] < 1/sensitivity:
            shift_points.append(i + 1)  # +1 because diff reduces index by 1
        # Also check for sudden return to normal (end of shifted section)
        elif i > 0 and (ratios[i-1] > sensitivity or ratios[i-1] < 1/sensitivity):
            if 0.5 < ratios[i] < 2.0:  # Back to normal
                shift_points.append(i + 1)
    
    return shift_points


def determine_bit_shift(chunk_timestamps: np.ndarray, reference_rate: float):
    """
    Determine the bit shift for a chunk by testing shifts.
    Returns the shift that brings the rate closest to reference_rate.
    """
    if len(chunk_timestamps) < 2:
        return 0
    
    chunk_rate = np.median(np.diff(chunk_timestamps))
    
    # Check if already aligned
    if 0.8 < chunk_rate / reference_rate < 1.2:
        return 0
    
    best_shift = 0
    best_ratio_error = float('inf')
    
    # Test shifts from -20 to +20
    for shift in range(-20, 21):
        if shift == 0:
            test_rate = chunk_rate
        elif shift > 0:
            # Left shift multiplies by 2^shift
            test_rate = chunk_rate * (2 ** shift)
        else:
            # Right shift divides by 2^|shift|
            test_rate = chunk_rate / (2 ** abs(shift))
        
        ratio = test_rate / reference_rate
        error = abs(np.log2(ratio)) if ratio > 0 else float('inf')
        
        if error < best_ratio_error:
            best_ratio_error = error
            best_shift = shift
    
    return best_shift


def segment_and_detect_shifts(timestamps: np.ndarray, 
                              min_chunk_size: int = 50,
                              sensitivity: float = 5.0):
    """
    Segment data into chunks and detect bit shift for each chunk.
    """
    # Get reference rate from early data (assumed to be correct)
    reference_data = timestamps[:min(1000, len(timestamps))]
    reference_rate = np.median(np.diff(reference_data))
    
    print(f"Reference timestamp rate: {reference_rate:.2f}")
    
    # Detect potential shift boundaries
    shift_points = detect_shift_points(timestamps, sensitivity=sensitivity)
    
    # Create boundaries
    boundaries = [0] + sorted(shift_points) + [len(timestamps)]
    
    # Merge small chunks
    merged_boundaries = [boundaries[0]]
    for i in range(1, len(boundaries)):
        if boundaries[i] - merged_boundaries[-1] >= min_chunk_size:
            merged_boundaries.append(boundaries[i])
        elif i == len(boundaries) - 1:
            merged_boundaries.append(boundaries[i])
    
    boundaries = merged_boundaries
    
    print(f"\nDetected {len(boundaries)-1} potential chunks")
    
    chunks = []
    
    for i in range(len(boundaries) - 1):
        start_idx = boundaries[i]
        end_idx = boundaries[i + 1]
        
        chunk_timestamps = timestamps[start_idx:end_idx]
        
        if len(chunk_timestamps) < 2:
            continue
        
        # Determine bit shift
        bit_shift = determine_bit_shift(chunk_timestamps, reference_rate)
        
        # Calculate chunk statistics
        chunk_diffs = np.diff(chunk_timestamps)
        median_rate = np.median(chunk_diffs)
        
        chunks.append({
            'start_idx': int(start_idx),
            'end_idx': int(end_idx),
            'length': int(end_idx - start_idx),
            'bit_shift': bit_shift,
            'first_timestamp': int(chunk_timestamps[0]),
            'last_timestamp': int(chunk_timestamps[-1]),
            'median_rate': float(median_rate),
            'rate_ratio': float(median_rate / reference_rate)
        })
    
    return chunks, reference_rate


def correct_bitshift(df: pd.DataFrame, 
                     timestamp_col: str = 'timestamp',
                     min_chunk_size: int = 50,
                     sensitivity: float = 5.0,
                     auto_correct: bool = True):
    """
    Detect and correct bit-shifted timestamps.
    
    Parameters:
    - df: Input dataframe
    - timestamp_col: Name of timestamp column
    - min_chunk_size: Minimum rows to consider a separate chunk
    - sensitivity: Higher = detect fewer shifts (5-10 recommended)
    - auto_correct: If True, apply corrections automatically
    """
    
    df = df.copy()
    timestamps = df[timestamp_col].values
    
    print(f"Analyzing {len(timestamps)} timestamps...")
    
    # Detect chunks and shifts
    chunks, reference_rate = segment_and_detect_shifts(
        timestamps, 
        min_chunk_size=min_chunk_size,
        sensitivity=sensitivity
    )
    
    print(f"\n{'='*70}")
    print(f"CHUNK ANALYSIS")
    print(f"{'='*70}\n")
    
    for i, chunk in enumerate(chunks):
        status = "✓ OK" if chunk['bit_shift'] == 0 else f"⚠ SHIFTED by {chunk['bit_shift']} bits"
        print(f"Chunk {i+1}: {status}")
        print(f"  Rows: {chunk['start_idx']:,} to {chunk['end_idx']-1:,} ({chunk['length']:,} rows)")
        print(f"  Timestamps: {chunk['first_timestamp']:,} → {chunk['last_timestamp']:,}")
        print(f"  Rate: {chunk['median_rate']:.2f} (ratio: {chunk['rate_ratio']:.3f}x)")
        if chunk['bit_shift'] != 0:
            print(f"  Correction: {'LEFT' if chunk['bit_shift'] > 0 else 'RIGHT'} shift by {abs(chunk['bit_shift'])} bits")
        print()
    
    # Reset index to ensure continuous indexing
    df = df.reset_index(drop=True)
    
    # Add diagnostic columns
    df['chunk_id'] = -1
    df['bit_shift_detected'] = 0
    df['corrected_timestamp'] = timestamps.copy()
    df['rate_ratio'] = 1.0
    
    # Apply corrections using iloc for position-based indexing
    for i, chunk in enumerate(chunks):
        start, end = chunk['start_idx'], chunk['end_idx']
        shift = chunk['bit_shift']
        
        df.iloc[start:end, df.columns.get_loc('chunk_id')] = i
        df.iloc[start:end, df.columns.get_loc('bit_shift_detected')] = shift
        df.iloc[start:end, df.columns.get_loc('rate_ratio')] = chunk['rate_ratio']
        
        if shift != 0 and auto_correct:
            if shift > 0:
                # Data was right-shifted, so left-shift to correct
                corrected_values = timestamps[start:end] << shift
            else:
                # Data was left-shifted, so right-shift to correct
                corrected_values = timestamps[start:end] >> abs(shift)
            
            df.iloc[start:end, df.columns.get_loc('corrected_timestamp')] = corrected_values
    
    return df, chunks


# Example usage
if __name__ == "__main__":
    # Load data
    df = pd.read_csv('strain_gauge_789.csv')
    
    print(f"Loaded {len(df):,} rows\n")
    
    # Detect and correct
    df_corrected, chunks = correct_bitshift(
        df, 
        timestamp_col='timestamp',
        min_chunk_size=50,      # Adjust if you expect smaller chunks
        sensitivity=5.0,         # Lower = more sensitive (3-10 range)
        auto_correct=True
    )
    
    # Remove rows where correction created zeros
    rows_before = len(df_corrected)
    df_corrected = df_corrected[df_corrected['corrected_timestamp'] > 0]
    rows_removed = rows_before - len(df_corrected)
    
    # Save results
    df_corrected.to_csv('strain_gauge_789_corrected.csv', index=False)
    print(f"\n{'='*70}")
    print(f"✓ Corrected data saved to 'strain_gauge_789_corrected.csv'")
    if rows_removed > 0:
        print(f"  Removed {rows_removed:,} rows with zero timestamps")
    print(f"{'='*70}\n")
    
    # Show summary statistics
    print("Summary:")
    print(f"  Total rows: {len(df):,}")
    print(f"  Chunks found: {len(chunks)}")
    print(f"  Chunks needing correction: {sum(1 for c in chunks if c['bit_shift'] != 0)}")
    
    # Show before/after comparison
    print("\nSample comparison (first 10 rows):")
    print(df_corrected[['timestamp', 'chunk_id', 'bit_shift_detected', 
                        'corrected_timestamp', 'rate_ratio']].head(10).to_string())