import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from scipy import stats

def analyze_timestamp_linearity(df: pd.DataFrame, 
                                timestamp_col: str = 'corrected_timestamp',
                                plot: bool = True):
    """
    Analyze if timestamps follow a roughly linear progression.
    
    Parameters:
    - df: Dataframe with timestamp data
    - timestamp_col: Column name to analyze
    - plot: Whether to generate visualizations
    
    Returns:
    - Dictionary with analysis results
    """
    
    timestamps = df[timestamp_col].values
    n = len(timestamps)
    indices = np.arange(n)
    
    print(f"{'='*70}")
    print(f"TIMESTAMP LINEARITY ANALYSIS")
    print(f"{'='*70}\n")
    print(f"Analyzing {n:,} timestamps\n")
    
    # 1. Basic Statistics
    print("=" * 70)
    print("1. BASIC STATISTICS")
    print("=" * 70)
    
    diffs = np.diff(timestamps)
    
    print(f"\nTimestamp range: {timestamps[0]:,} to {timestamps[-1]:,}")
    print(f"Total span: {timestamps[-1] - timestamps[0]:,}")
    print(f"\nTimestamp Increments (differences):")
    print(f"  Mean:   {np.mean(diffs):,.2f}")
    print(f"  Median: {np.median(diffs):,.2f}")
    print(f"  Std:    {np.std(diffs):,.2f}")
    print(f"  Min:    {np.min(diffs):,}")
    print(f"  Max:    {np.max(diffs):,}")
    print(f"  Q1:     {np.percentile(diffs, 25):,.2f}")
    print(f"  Q3:     {np.percentile(diffs, 75):,.2f}")
    
    # 2. Linear Fit Analysis
    print(f"\n{'='*70}")
    print("2. LINEAR FIT ANALYSIS")
    print("=" * 70)
    
    # Perform linear regression
    slope, intercept, r_value, p_value, std_err = stats.linregress(indices, timestamps)
    
    print(f"\nLinear regression: timestamp = {slope:.2f} * index + {intercept:.2f}")
    print(f"  R² (coefficient of determination): {r_value**2:.6f}")
    print(f"  R (correlation coefficient):        {r_value:.6f}")
    print(f"  Standard error:                     {std_err:.2f}")
    
    # Calculate predicted values and residuals
    predicted = slope * indices + intercept
    residuals = timestamps - predicted
    
    print(f"\nResidual statistics:")
    print(f"  Mean residual:     {np.mean(residuals):,.2f}")
    print(f"  Std of residuals:  {np.std(residuals):,.2f}")
    print(f"  Max deviation:     {np.max(np.abs(residuals)):,.2f}")
    print(f"  95th percentile:   {np.percentile(np.abs(residuals), 95):,.2f}")
    
    # 3. Monotonicity Check
    print(f"\n{'='*70}")
    print("3. MONOTONICITY CHECK")
    print("=" * 70)
    
    non_increasing = np.sum(diffs <= 0)
    negative = np.sum(diffs < 0)
    
    print(f"\nNon-increasing steps: {non_increasing:,} ({100*non_increasing/len(diffs):.2f}%)")
    print(f"Negative steps:       {negative:,} ({100*negative/len(diffs):.2f}%)")
    
    if non_increasing > 0:
        print(f"\n⚠ Warning: Found {non_increasing} non-increasing timestamp(s)")
        problem_indices = np.where(diffs <= 0)[0]
        print(f"\nFirst 10 problem locations:")
        for idx in problem_indices[:10]:
            print(f"  Index {idx}: {timestamps[idx]:,} → {timestamps[idx+1]:,} (diff: {diffs[idx]:,})")
    else:
        print(f"\n✓ All timestamps are strictly increasing")
    
    # 4. Outlier Detection
    print(f"\n{'='*70}")
    print("4. OUTLIER DETECTION")
    print("=" * 70)
    
    # Use IQR method on differences
    q1 = np.percentile(diffs, 25)
    q3 = np.percentile(diffs, 75)
    iqr = q3 - q1
    lower_bound = q1 - 3 * iqr
    upper_bound = q3 + 3 * iqr
    
    outliers = (diffs < lower_bound) | (diffs > upper_bound)
    n_outliers = np.sum(outliers)
    
    print(f"\nUsing IQR method (3× IQR):")
    print(f"  Expected range: [{lower_bound:.2f}, {upper_bound:.2f}]")
    print(f"  Outliers found: {n_outliers:,} ({100*n_outliers/len(diffs):.2f}%)")
    
    if n_outliers > 0:
        outlier_indices = np.where(outliers)[0]
        print(f"\nFirst 10 outliers:")
        for idx in outlier_indices[:10]:
            print(f"  Index {idx}: diff = {diffs[idx]:,} (expected ~{np.median(diffs):.0f})")
    
    # 5. Consistency Across Data
    print(f"\n{'='*70}")
    print("5. CONSISTENCY ACROSS DATA")
    print("=" * 70)
    
    # Divide data into segments and check consistency
    n_segments = 10
    segment_size = n // n_segments
    segment_stats = []
    
    print(f"\nAnalyzing {n_segments} segments of ~{segment_size:,} rows each:")
    print(f"\n{'Segment':<10} {'Start Row':<12} {'Mean Diff':<12} {'Std Diff':<12} {'R²':<10}")
    print("-" * 70)
    
    for i in range(n_segments):
        start_idx = i * segment_size
        end_idx = min((i + 1) * segment_size, n)
        
        seg_timestamps = timestamps[start_idx:end_idx]
        seg_indices = np.arange(len(seg_timestamps))
        seg_diffs = np.diff(seg_timestamps)
        
        if len(seg_timestamps) > 1:
            seg_slope, seg_intercept, seg_r, _, _ = stats.linregress(seg_indices, seg_timestamps)
            
            segment_stats.append({
                'start': start_idx,
                'mean_diff': np.mean(seg_diffs),
                'std_diff': np.std(seg_diffs),
                'r_squared': seg_r**2
            })
            
            print(f"{i+1:<10} {start_idx:<12,} {np.mean(seg_diffs):<12.2f} {np.std(seg_diffs):<12.2f} {seg_r**2:<10.6f}")
    
    # Overall Assessment
    print(f"\n{'='*70}")
    print("6. OVERALL ASSESSMENT")
    print("=" * 70)
    
    issues = []
    
    if r_value**2 < 0.95:
        issues.append(f"Low R² ({r_value**2:.4f}) - timestamps may not be linear")
    
    if non_increasing > n * 0.001:  # More than 0.1% non-increasing
        issues.append(f"Too many non-increasing steps ({non_increasing})")
    
    if n_outliers > n * 0.05:  # More than 5% outliers
        issues.append(f"High outlier rate ({100*n_outliers/len(diffs):.1f}%)")
    
    # Check consistency across segments
    segment_r_squares = [s['r_squared'] for s in segment_stats]
    if np.min(segment_r_squares) < 0.90:
        issues.append(f"Some segments have low R² (min: {np.min(segment_r_squares):.4f})")
    
    print()
    if len(issues) == 0:
        print("✓ PASS: Timestamps follow a roughly linear progression")
        print(f"  - High correlation (R² = {r_value**2:.6f})")
        print(f"  - Consistent increments (CV = {100*np.std(diffs)/np.mean(diffs):.1f}%)")
        print(f"  - Monotonically increasing")
    else:
        print("⚠ ISSUES DETECTED:")
        for issue in issues:
            print(f"  - {issue}")
    
    # 7. Visualizations
    if plot:
        print(f"\n{'='*70}")
        print("7. GENERATING VISUALIZATIONS")
        print("=" * 70)
        
        fig, axes = plt.subplots(2, 2, figsize=(15, 10))
        
        # Plot 1: Timestamps vs Index
        ax = axes[0, 0]
        ax.plot(indices, timestamps, 'b-', alpha=0.5, linewidth=0.5, label='Actual')
        ax.plot(indices, predicted, 'r--', linewidth=2, label='Linear Fit')
        ax.set_xlabel('Row Index')
        ax.set_ylabel('Timestamp')
        ax.set_title(f'Timestamps vs Index (R² = {r_value**2:.6f})')
        ax.legend()
        ax.grid(True, alpha=0.3)
        
        # Plot 2: Residuals
        ax = axes[0, 1]
        ax.scatter(indices, residuals, alpha=0.3, s=1)
        ax.axhline(y=0, color='r', linestyle='--', linewidth=2)
        ax.set_xlabel('Row Index')
        ax.set_ylabel('Residual (Actual - Predicted)')
        ax.set_title('Residual Plot')
        ax.grid(True, alpha=0.3)
        
        # Plot 3: Histogram of Differences
        ax = axes[1, 0]
        ax.hist(diffs, bins=100, edgecolor='black', alpha=0.7)
        ax.axvline(np.median(diffs), color='r', linestyle='--', linewidth=2, label=f'Median: {np.median(diffs):.2f}')
        ax.axvline(np.mean(diffs), color='g', linestyle='--', linewidth=2, label=f'Mean: {np.mean(diffs):.2f}')
        ax.set_xlabel('Timestamp Increment')
        ax.set_ylabel('Frequency')
        ax.set_title('Distribution of Timestamp Increments')
        ax.legend()
        ax.grid(True, alpha=0.3)
        
        # Plot 4: Moving average of differences
        ax = axes[1, 1]
        window = min(1000, len(diffs) // 10)
        moving_avg = pd.Series(diffs).rolling(window=window, center=True).mean()
        ax.plot(moving_avg, 'b-', linewidth=1)
        ax.axhline(np.median(diffs), color='r', linestyle='--', linewidth=2, label='Overall Median')
        ax.set_xlabel('Row Index')
        ax.set_ylabel('Timestamp Increment')
        ax.set_title(f'Moving Average of Increments (window={window})')
        ax.legend()
        ax.grid(True, alpha=0.3)
        
        plt.tight_layout()
        plt.savefig('timestamp_linearity_analysis.png', dpi=150, bbox_inches='tight')
        print("\n✓ Plots saved to 'timestamp_linearity_analysis.png'")
        plt.show()
    
    # Return results
    return {
        'n_rows': n,
        'r_squared': r_value**2,
        'slope': slope,
        'intercept': intercept,
        'mean_increment': np.mean(diffs),
        'median_increment': np.median(diffs),
        'std_increment': np.std(diffs),
        'non_increasing_count': non_increasing,
        'outlier_count': n_outliers,
        'residual_std': np.std(residuals),
        'passes_linearity': len(issues) == 0,
        'issues': issues
    }


# Example usage
if __name__ == "__main__":
    # Load the corrected data
    df = pd.read_csv('strain_gauge_123_corrected.csv')
    
    print(f"Loaded {len(df):,} rows from corrected data\n")
    
    # Analyze the corrected timestamps
    results = analyze_timestamp_linearity(
        df, 
        timestamp_col='corrected_timestamp',
        plot=True
    )
    
    print(f"\n{'='*70}")
    print("Analysis complete!")
    print(f"{'='*70}\n")